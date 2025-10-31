module;

// No standard library includes - we're freestanding!
#include "../include/freestanding/types.hpp"

export module hft.core;

export namespace hft {

// Re-export our basic types
using hft::uint8_t;
using hft::uint16_t;
using hft::uint32_t;
using hft::uint64_t;
using hft::int8_t;
using hft::int16_t;
using hft::int32_t;
using hft::int64_t;
using hft::size_t;
using hft::ptrdiff_t;
using hft::uintptr_t;
using hft::intptr_t;

// Using C++26 pack indexing (P2662R3)
template<typename... Ts>
struct type_list {
    static constexpr size_t size = sizeof...(Ts);
    
    template<size_t I>
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
    // Size check removed - types may span cache lines
};

// Hardware features detection
struct cpu_features {
    bool avx512f : 1;
    bool avx512dq : 1;
    bool avx512vl : 1;
    bool tsx : 1;
    bool cet : 1;
    uint8_t reserved : 3;
    
    static cpu_features detect() noexcept;
};

// Timestamp counter wrapper
class tsc_clock {
public:
    using rep = uint64_t;
    
    static rep now() noexcept {
        uint32_t lo, hi;
        // Fixed: Added '=' to the "d" constraint
        asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
        return (static_cast<rep>(hi) << 32) | lo;
    }
};

// Using C++26 constexpr placement new (P2747R2)
template<typename T, size_t N>
class static_pool {
    alignas(T) unsigned char storage[sizeof(T) * N];
    size_t next_free = 0;
    
public:
    [[nodiscard]] constexpr T* allocate() noexcept {
        if (next_free >= N) [[unlikely]] {
            return nullptr;
        }
        return construct_at(
            reinterpret_cast<T*>(&storage[sizeof(T) * next_free++])
        );
    }
    
    constexpr void deallocate(T* ptr) noexcept {
        destroy_at(ptr);
    }
};

// Core kernel interface
class kernel {
public:
    [[noreturn]] static void panic(const char* msg) noexcept;
    static void initialize() noexcept;
    static cpu_features get_cpu_features() noexcept;
};

// Terminal output for early boot
namespace terminal {
    void write(const char* str) noexcept;
    void write_hex(uint64_t value) noexcept;
    void clear() noexcept;
}

} // namespace hft
