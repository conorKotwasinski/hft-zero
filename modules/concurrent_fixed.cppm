module;

#include "../include/freestanding/types.hpp"
#include "../include/freestanding/atomic.hpp"

export module hft.concurrent;
import hft.core;

export namespace hft::concurrent {

// Single Producer Single Consumer lock-free queue
template<typename T, size_t Size>
    requires (Size > 0) && ((Size & (Size - 1)) == 0)  // Power of 2
class spsc_queue : private cache_aligned<spsc_queue<T, Size>> {
public:
    static constexpr size_t capacity = Size;
    
private:
    // Separate cache lines for producer and consumer
    alignas(64) atomic<size_t> write_idx_{0};
    alignas(64) atomic<size_t> read_idx_{0};
    alignas(64) T buffer_[Size];
    
    static constexpr size_t index_mask = Size - 1;
    
public:
    constexpr spsc_queue() noexcept = default;
    
    // Producer interface
    [[nodiscard]] bool try_push(const T& item) noexcept {
        const auto write_pos = write_idx_.load(memory_order::relaxed);
        const auto next_pos = (write_pos + 1) & index_mask;
        
        if (next_pos == read_idx_.load(memory_order::acquire)) {
            return false;  // Queue full
        }
        
        buffer_[write_pos] = item;
        write_idx_.store(next_pos, memory_order::release);
        return true;
    }
    
    template<typename... Args>
    [[nodiscard]] bool try_emplace(Args&&... args) noexcept {
        const auto write_pos = write_idx_.load(memory_order::relaxed);
        const auto next_pos = (write_pos + 1) & index_mask;
        
        if (next_pos == read_idx_.load(memory_order::acquire)) {
            return false;  // Queue full
        }
        
        construct_at(&buffer_[write_pos], forward<Args>(args)...);
        write_idx_.store(next_pos, memory_order::release);
        return true;
    }
    
    // Consumer interface
    [[nodiscard]] bool try_pop(T& item) noexcept {
        const auto read_pos = read_idx_.load(memory_order::relaxed);
        
        if (read_pos == write_idx_.load(memory_order::acquire)) {
            return false;  // Queue empty
        }
        
        item = move(buffer_[read_pos]);
        read_idx_.store((read_pos + 1) & index_mask, memory_order::release);
        return true;
    }
    
    // Bulk operations for better throughput
    [[nodiscard]] size_t try_push_bulk(const T* items, size_t count) noexcept {
        const auto write_pos = write_idx_.load(memory_order::relaxed);
        const auto read_pos = read_idx_.load(memory_order::acquire);
        
        const size_t available = (read_pos - write_pos - 1) & index_mask;
        const size_t to_write = min(count, available);
        
        if (to_write == 0) {
            return 0;
        }
        
        // Copy in two parts if wrapping
        const size_t first_part = min(to_write, Size - write_pos);
        memcpy(&buffer_[write_pos], items, first_part * sizeof(T));
        
        if (to_write > first_part) {
            memcpy(&buffer_[0], items + first_part, 
                   (to_write - first_part) * sizeof(T));
        }
        
        write_idx_.store((write_pos + to_write) & index_mask, 
                        memory_order::release);
        return to_write;
    }
    
    [[nodiscard]] size_t size() const noexcept {
        const auto write_pos = write_idx_.load(memory_order::acquire);
        const auto read_pos = read_idx_.load(memory_order::acquire);
        return (write_pos - read_pos) & index_mask;
    }
    
    [[nodiscard]] bool empty() const noexcept {
        return read_idx_.load(memory_order::acquire) == 
               write_idx_.load(memory_order::acquire);
    }
    
    [[nodiscard]] bool full() const noexcept {
        const auto write_pos = write_idx_.load(memory_order::acquire);
        const auto read_pos = read_idx_.load(memory_order::acquire);
        return ((write_pos + 1) & index_mask) == read_pos;
    }
};

// Multi-producer single consumer queue using CAS
template<typename T, size_t Size>
class mpsc_queue : private cache_aligned<mpsc_queue<T, Size>> {
    struct node {
        atomic<node*> next{nullptr};
        T data;
    };
    
    alignas(64) atomic<node*> head_;
    alignas(64) node* tail_;
    static_pool<node, Size> pool_;
    
public:
    mpsc_queue() noexcept : head_(nullptr), tail_(nullptr) {}
    
    [[nodiscard]] bool try_push(const T& item) noexcept {
        auto* new_node = pool_.allocate();
        if (!new_node) return false;
        
        new_node->data = item;
        new_node->next.store(nullptr, memory_order::relaxed);
        
        node* prev_head = head_.exchange(new_node, memory_order::acq_rel);
        if (prev_head) {
            prev_head->next.store(new_node, memory_order::release);
        } else {
            tail_ = new_node;
        }
        
        return true;
    }
    
    [[nodiscard]] bool try_pop(T& item) noexcept {
        if (!tail_) {
            return false;
        }
        
        item = move(tail_->data);
        node* next = tail_->next.load(memory_order::acquire);
        
        pool_.deallocate(tail_);
        tail_ = next;
        
        if (!tail_) {
            head_.store(nullptr, memory_order::release);
        }
        
        return true;
    }
};

} // namespace hft::concurrent
