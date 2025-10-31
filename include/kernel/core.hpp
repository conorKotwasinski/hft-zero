#pragma once

#include <cstdint>
#include <cstddef>
#include <concepts>
#include <type_traits>
#include <bit>

namespace hft {

// Using C++26 pack indexing (P2662R3)
template<typename... Ts>
struct type_list {
    static constexpr std::size_t size = sizeof...(Ts);
    
    template<std::size_t I>
    using type = Ts...[I];
};

// Using C++26 delete with reason (P2573R2)
class non_copyable {
public:
    non_copyable() = default;
    non_copyable(const non_copyable&) = delete("Object is non-copyable for performance");
    non_copyable& operator=(const non_copyable&) = delete("Object is non-copyable for performance");
    non_copyable(non_copyable&&) = default;
    non_copyable& operator=(non_copyable&&) = default;
};

// Cache-aligned base class
template<typename T>
class alignas(64) cache_aligned : public non_copyable {
protected:
    static_assert(sizeof(T) <= 64, "Type too large for single cache line");
};

// Hardware features detection
struct cpu_features {
    bool avx512f : 1;
    bool avx512dq : 1;
    bool avx512vl : 1;
    bool tsx : 1;
    bool cet : 1;
    std::uint8_t reserved : 3;
    
    static cpu_features detect() noexcept;
};

// Timestamp counter wrapper
class tsc_clock {
public:
    using rep = std::uint64_t;
    using period = std::nano;
    using duration = std::chrono::duration<rep, period>;
    using time_point = std::chrono::time_point<tsc_clock>;
    
    static time_point now() noexcept {
        std::uint32_t lo, hi;
        asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
        return time_point{duration{(static_cast<rep>(hi) << 32) | lo}};
    }
};

// Using C++26 constexpr placement new (P2747R2)
template<typename T, std::size_t N>
class static_pool {
    alignas(T) std::byte storage[sizeof(T) * N];
    std::size_t next_free = 0;
    
public:
    [[nodiscard]] constexpr T* allocate() noexcept {
        if (next_free >= N) [[unlikely]] {
            return nullptr;
        }
        return std::construct_at(
            reinterpret_cast<T*>(&storage[sizeof(T) * next_free++])
        );
    }
    
    constexpr void deallocate(T* ptr) noexcept {
        std::destroy_at(ptr);
    }
};

// Core kernel interface
class kernel {
public:
    [[noreturn]] static void panic(const char* msg) noexcept;
    static void initialize() noexcept;
    static cpu_features get_cpu_features() noexcept;
};

} // namespace hft
