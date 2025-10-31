#pragma once
// Basic type definitions for freestanding environment
// No standard library dependencies

// Define global size_t using compiler built-in (required for placement new)
using size_t = __SIZE_TYPE__;

namespace hft {
// Basic integer types
using uint8_t = unsigned char;
using uint16_t = unsigned short;
using uint32_t = unsigned int;
using uint64_t = unsigned long long;

using int8_t = signed char;
using int16_t = signed short;
using int32_t = signed int;
using int64_t = signed long long;

// Size types
using size_t = uint64_t;
using ptrdiff_t = int64_t;
using uintptr_t = uint64_t;
using intptr_t = int64_t;

// Null pointer
constexpr decltype(nullptr) null = nullptr;

// Basic memory operations
inline void* memset(void* dest, int ch, size_t count) {
    auto* d = static_cast<unsigned char*>(dest);
    while (count--) {
        *d++ = static_cast<unsigned char>(ch);
    }
    return dest;
}

inline void* memcpy(void* dest, const void* src, size_t count) {
    auto* d = static_cast<unsigned char*>(dest);
    const auto* s = static_cast<const unsigned char*>(src);
    while (count--) {
        *d++ = *s++;
    }
    return dest;
}

// Construct at
template<typename T, typename... Args>
constexpr T* construct_at(T* ptr, Args&&... args) {
    return ::new(static_cast<void*>(ptr)) T(static_cast<Args&&>(args)...);
}

// Destroy at
template<typename T>
constexpr void destroy_at(T* ptr) {
    ptr->~T();
}

// Move
template<typename T>
constexpr T&& move(T& t) noexcept {
    return static_cast<T&&>(t);
}

// Forward
template<typename T>
constexpr T&& forward(T& t) noexcept {
    return static_cast<T&&>(t);
}

// Min/Max
template<typename T>
constexpr const T& min(const T& a, const T& b) {
    return (b < a) ? b : a;
}

template<typename T>
constexpr const T& max(const T& a, const T& b) {
    return (a < b) ? b : a;
}
} // namespace hft

// Placement new/delete operators (must be in global namespace)
// MUST use global ::size_t (compiler built-in), not hft::size_t
inline void* operator new(::size_t, void* ptr) noexcept {
    return ptr;
}

inline void* operator new[](::size_t, void* ptr) noexcept {
    return ptr;
}

// Delete operators for placement new (no-op)
inline void operator delete(void*, void*) noexcept {}
inline void operator delete[](void*, void*) noexcept {}

