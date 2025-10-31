#pragma once

#include "types.hpp"

namespace hft {

// Memory ordering
enum class memory_order {
    relaxed,
    acquire,
    release,
    acq_rel,
    seq_cst
};

// Basic atomic template using compiler builtins
template<typename T>
class atomic {
    volatile T value;
    
public:
    atomic() noexcept = default;
    explicit atomic(T val) noexcept : value(val) {}
    
    // Delete copy operations
    atomic(const atomic&) = delete;
    atomic& operator=(const atomic&) = delete;
    
    T load(memory_order order = memory_order::seq_cst) const noexcept {
        T result;
        switch (order) {
            case memory_order::relaxed:
                result = __atomic_load_n(&value, __ATOMIC_RELAXED);
                break;
            case memory_order::acquire:
                result = __atomic_load_n(&value, __ATOMIC_ACQUIRE);
                break;
            default:
                result = __atomic_load_n(&value, __ATOMIC_SEQ_CST);
        }
        return result;
    }
    
    void store(T val, memory_order order = memory_order::seq_cst) noexcept {
        switch (order) {
            case memory_order::relaxed:
                __atomic_store_n(&value, val, __ATOMIC_RELAXED);
                break;
            case memory_order::release:
                __atomic_store_n(&value, val, __ATOMIC_RELEASE);
                break;
            default:
                __atomic_store_n(&value, val, __ATOMIC_SEQ_CST);
        }
    }
    
    T exchange(T val, memory_order order = memory_order::seq_cst) noexcept {
        return __atomic_exchange_n(&value, val, 
            order == memory_order::relaxed ? __ATOMIC_RELAXED :
            order == memory_order::acquire ? __ATOMIC_ACQUIRE :
            order == memory_order::release ? __ATOMIC_RELEASE :
            order == memory_order::acq_rel ? __ATOMIC_ACQ_REL :
            __ATOMIC_SEQ_CST);
    }
    
    T fetch_add(T arg, memory_order order = memory_order::seq_cst) noexcept {
        return __atomic_fetch_add(&value, arg,
            order == memory_order::relaxed ? __ATOMIC_RELAXED :
            order == memory_order::acquire ? __ATOMIC_ACQUIRE :
            order == memory_order::release ? __ATOMIC_RELEASE :
            order == memory_order::acq_rel ? __ATOMIC_ACQ_REL :
            __ATOMIC_SEQ_CST);
    }
    
    bool compare_exchange_weak(T& expected, T desired,
                               memory_order order = memory_order::seq_cst) noexcept {
        return __atomic_compare_exchange_n(&value, &expected, desired,
            true,  // weak
            order == memory_order::relaxed ? __ATOMIC_RELAXED :
            order == memory_order::acquire ? __ATOMIC_ACQUIRE :
            order == memory_order::release ? __ATOMIC_RELEASE :
            order == memory_order::acq_rel ? __ATOMIC_ACQ_REL :
            __ATOMIC_SEQ_CST,
            __ATOMIC_RELAXED);
    }
};

// Specialization for pointer types
template<typename T>
class atomic<T*> {
    volatile T* value;
    
public:
    atomic() noexcept = default;
    explicit atomic(T* val) noexcept : value(val) {}
    
    T* load(memory_order order = memory_order::seq_cst) const noexcept {
        return __atomic_load_n(&value,
            order == memory_order::relaxed ? __ATOMIC_RELAXED :
            order == memory_order::acquire ? __ATOMIC_ACQUIRE :
            __ATOMIC_SEQ_CST);
    }
    
    void store(T* val, memory_order order = memory_order::seq_cst) noexcept {
        __atomic_store_n(&value, val,
            order == memory_order::relaxed ? __ATOMIC_RELAXED :
            order == memory_order::release ? __ATOMIC_RELEASE :
            __ATOMIC_SEQ_CST);
    }
    
    T* exchange(T* val, memory_order order = memory_order::seq_cst) noexcept {
        return __atomic_exchange_n(&value, val,
            order == memory_order::relaxed ? __ATOMIC_RELAXED :
            order == memory_order::acquire ? __ATOMIC_ACQUIRE :
            order == memory_order::release ? __ATOMIC_RELEASE :
            order == memory_order::acq_rel ? __ATOMIC_ACQ_REL :
            __ATOMIC_SEQ_CST);
    }
};

} // namespace hft
