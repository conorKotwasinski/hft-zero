#pragma once

#include <atomic>
#include <cstddef>
#include <cstring>
#include <new>
#include "kernel/core.hpp"

namespace hft::concurrent {

// Single Producer Single Consumer lock-free queue
template<typename T, std::size_t Size>
    requires (Size > 0) && ((Size & (Size - 1)) == 0)  // Power of 2
class spsc_queue : private cache_aligned<spsc_queue<T, Size>> {
public:
    static constexpr std::size_t capacity = Size;
    
private:
    // Separate cache lines for producer and consumer
    alignas(64) std::atomic<std::size_t> write_idx_{0};
    alignas(64) std::atomic<std::size_t> read_idx_{0};
    alignas(64) T buffer_[Size];
    
    static constexpr std::size_t index_mask = Size - 1;
    
public:
    constexpr spsc_queue() noexcept = default;
    
    // Producer interface
    [[nodiscard]] bool try_push(const T& item) noexcept {
        const auto write_pos = write_idx_.load(std::memory_order_relaxed);
        const auto next_pos = (write_pos + 1) & index_mask;
        
        if (next_pos == read_idx_.load(std::memory_order_acquire)) {
            return false;  // Queue full
        }
        
        buffer_[write_pos] = item;
        write_idx_.store(next_pos, std::memory_order_release);
        return true;
    }
    
    template<typename... Args>
    [[nodiscard]] bool try_emplace(Args&&... args) noexcept {
        const auto write_pos = write_idx_.load(std::memory_order_relaxed);
        const auto next_pos = (write_pos + 1) & index_mask;
        
        if (next_pos == read_idx_.load(std::memory_order_acquire)) {
            return false;  // Queue full
        }
        
        std::construct_at(&buffer_[write_pos], std::forward<Args>(args)...);
        write_idx_.store(next_pos, std::memory_order_release);
        return true;
    }
    
    // Consumer interface
    [[nodiscard]] bool try_pop(T& item) noexcept {
        const auto read_pos = read_idx_.load(std::memory_order_relaxed);
        
        if (read_pos == write_idx_.load(std::memory_order_acquire)) {
            return false;  // Queue empty
        }
        
        item = std::move(buffer_[read_pos]);
        read_idx_.store((read_pos + 1) & index_mask, std::memory_order_release);
        return true;
    }
    
    // Bulk operations for better throughput
    [[nodiscard]] std::size_t try_push_bulk(const T* items, std::size_t count) noexcept {
        const auto write_pos = write_idx_.load(std::memory_order_relaxed);
        const auto read_pos = read_idx_.load(std::memory_order_acquire);
        
        const std::size_t available = (read_pos - write_pos - 1) & index_mask;
        const std::size_t to_write = std::min(count, available);
        
        if (to_write == 0) {
            return 0;
        }
        
        // Copy in two parts if wrapping
        const std::size_t first_part = std::min(to_write, Size - write_pos);
        std::memcpy(&buffer_[write_pos], items, first_part * sizeof(T));
        
        if (to_write > first_part) {
            std::memcpy(&buffer_[0], items + first_part, 
                       (to_write - first_part) * sizeof(T));
        }
        
        write_idx_.store((write_pos + to_write) & index_mask, 
                        std::memory_order_release);
        return to_write;
    }
    
    [[nodiscard]] std::size_t size() const noexcept {
        const auto write_pos = write_idx_.load(std::memory_order_acquire);
        const auto read_pos = read_idx_.load(std::memory_order_acquire);
        return (write_pos - read_pos) & index_mask;
    }
    
    [[nodiscard]] bool empty() const noexcept {
        return read_idx_.load(std::memory_order_acquire) == 
               write_idx_.load(std::memory_order_acquire);
    }
    
    [[nodiscard]] bool full() const noexcept {
        const auto write_pos = write_idx_.load(std::memory_order_acquire);
        const auto read_pos = read_idx_.load(std::memory_order_acquire);
        return ((write_pos + 1) & index_mask) == read_pos;
    }
};

// Multi-producer single consumer queue using CAS
template<typename T, std::size_t Size>
class mpsc_queue : private cache_aligned<mpsc_queue<T, Size>> {
    struct node {
        std::atomic<node*> next{nullptr};
        T data;
    };
    
    alignas(64) std::atomic<node*> head_;
    alignas(64) node* tail_;
    static_pool<node, Size> pool_;
    
public:
    mpsc_queue() noexcept : head_(nullptr), tail_(nullptr) {}
    
    [[nodiscard]] bool try_push(const T& item) noexcept {
        auto* new_node = pool_.allocate();
        if (!new_node) return false;
        
        new_node->data = item;
        new_node->next.store(nullptr, std::memory_order_relaxed);
        
        node* prev_head = head_.exchange(new_node, std::memory_order_acq_rel);
        if (prev_head) {
            prev_head->next.store(new_node, std::memory_order_release);
        } else {
            tail_ = new_node;
        }
        
        return true;
    }
    
    [[nodiscard]] bool try_pop(T& item) noexcept {
        if (!tail_) {
            return false;
        }
        
        item = std::move(tail_->data);
        node* next = tail_->next.load(std::memory_order_acquire);
        
        pool_.deallocate(tail_);
        tail_ = next;
        
        if (!tail_) {
            head_.store(nullptr, std::memory_order_release);
        }
        
        return true;
    }
};

} // namespace hft::concurrent
