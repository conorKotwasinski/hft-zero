module;
#include "../include/freestanding/types.hpp"
#include "../include/freestanding/atomic.hpp"

export module hft.heap;
import hft.vmm;
import hft.pmm;

export namespace hft::heap {
using namespace hft;

constexpr size_t slab_sizes[] = {
    16, 32, 64, 128, 256, 512, 1024, 2048, 4096
};
constexpr size_t num_slabs = sizeof(slab_sizes) / sizeof(slab_sizes[0]);

class slab_allocator {
private:
    struct slab_header {
        slab_header* next;
        size_t size;
        size_t free_count;
        uint64_t free_bitmap[8];
    };
    
    size_t object_size;
    size_t objects_per_slab;
    slab_header* partial_slabs;
    slab_header* full_slabs;
    slab_header* empty_slabs;
    atomic<size_t> total_objects;
    atomic<size_t> free_objects;
    
public:
    void init(size_t size) noexcept;
    void* allocate() noexcept;
    void free(void* ptr) noexcept;
    size_t get_object_size() const noexcept { return object_size; }
    size_t get_free_objects() const noexcept { 
        return free_objects.load(memory_order::relaxed); 
    }
    size_t get_total_objects() const noexcept { 
        return total_objects.load(memory_order::relaxed); 
    }
    
private:
    slab_header* create_slab() noexcept;
    void* allocate_from_slab(slab_header* slab) noexcept;
    void move_slab_to_list(slab_header* slab, slab_header** list) noexcept;
};

struct large_allocation {
    large_allocation* next;
    large_allocation* prev;
    size_t size;
    size_t pages;
};

struct heap_stats {
    size_t used;
    size_t allocated;
    size_t slab_free[num_slabs];
    size_t slab_total[num_slabs];
};

inline slab_allocator slabs[num_slabs];
inline large_allocation* large_allocations;
inline atomic<size_t> heap_used;
inline atomic<size_t> heap_allocated;

void init() noexcept;
void* kmalloc(size_t size) noexcept;
void kfree(void* ptr) noexcept;
heap_stats get_stats() noexcept;
slab_allocator* find_slab(size_t size) noexcept;

// Implementation inline in module
void slab_allocator::init(size_t size) noexcept {
    object_size = size;
    objects_per_slab = (pmm::PAGE_SIZE - sizeof(slab_header)) / size;
    if (objects_per_slab > 512) objects_per_slab = 512;
    
    partial_slabs = nullptr;
    full_slabs = nullptr;
    empty_slabs = nullptr;
    total_objects.store(0, memory_order::relaxed);
    free_objects.store(0, memory_order::relaxed);
}

void* slab_allocator::allocate() noexcept {
    if (partial_slabs) return allocate_from_slab(partial_slabs);
    
    if (empty_slabs) {
        slab_header* slab = empty_slabs;
        empty_slabs = slab->next;
        slab->next = partial_slabs;
        partial_slabs = slab;
        return allocate_from_slab(slab);
    }
    
    slab_header* slab = create_slab();
    if (!slab) return nullptr;
    
    slab->next = partial_slabs;
    partial_slabs = slab;
    return allocate_from_slab(slab);
}

void slab_allocator::free(void* ptr) noexcept {
    if (!ptr) return;
    
    uint64_t addr = reinterpret_cast<uint64_t>(ptr);
    uint64_t slab_addr = addr & ~(pmm::PAGE_SIZE - 1);
    slab_header* slab = reinterpret_cast<slab_header*>(slab_addr);
    
    uint64_t offset = addr - slab_addr - sizeof(slab_header);
    size_t obj_idx = offset / object_size;
    
    size_t bitmap_idx = obj_idx / 64;
    size_t bit_idx = obj_idx % 64;
    slab->free_bitmap[bitmap_idx] |= (1ULL << bit_idx);
    slab->free_count++;
    free_objects.fetch_add(1, memory_order::relaxed);
    
    if (slab->free_count == objects_per_slab) {
        move_slab_to_list(slab, &empty_slabs);
    } else if (slab->free_count == 1) {
        move_slab_to_list(slab, &partial_slabs);
    }
}

slab_allocator::slab_header* slab_allocator::create_slab() noexcept {
    uint64_t virt = vmm::allocate_region(1);
    if (!virt) return nullptr;
    
    slab_header* slab = reinterpret_cast<slab_header*>(virt);
    slab->next = nullptr;
    slab->size = object_size;
    slab->free_count = objects_per_slab;
    
    for (size_t i = 0; i < 8; ++i) {
        if (i * 64 < objects_per_slab) {
            slab->free_bitmap[i] = (objects_per_slab - i * 64 >= 64) ? 
                0xFFFFFFFFFFFFFFFF : 
                (1ULL << (objects_per_slab - i * 64)) - 1;
        } else {
            slab->free_bitmap[i] = 0;
        }
    }
    
    total_objects.fetch_add(objects_per_slab, memory_order::relaxed);
    free_objects.fetch_add(objects_per_slab, memory_order::relaxed);
    
    return slab;
}

void* slab_allocator::allocate_from_slab(slab_header* slab) noexcept {
    for (size_t i = 0; i < 8; ++i) {
        if (slab->free_bitmap[i]) {
            int bit = __builtin_ctzll(slab->free_bitmap[i]);
            slab->free_bitmap[i] &= ~(1ULL << bit);
            slab->free_count--;
            free_objects.fetch_add(-1, memory_order::relaxed);
            
            uint64_t slab_addr = reinterpret_cast<uint64_t>(slab);
            uint64_t obj_addr = slab_addr + sizeof(slab_header) + 
                               (i * 64 + bit) * object_size;
            
            if (slab->free_count == 0) {
                move_slab_to_list(slab, &full_slabs);
            }
            
            return reinterpret_cast<void*>(obj_addr);
        }
    }
    return nullptr;
}

void slab_allocator::move_slab_to_list(slab_header* slab, slab_header** list) noexcept {
    if (slab == partial_slabs) {
        partial_slabs = slab->next;
    } else if (slab == full_slabs) {
        full_slabs = slab->next;
    } else if (slab == empty_slabs) {
        empty_slabs = slab->next;
    }
    
    slab->next = *list;
    *list = slab;
}

void init() noexcept {
    for (size_t i = 0; i < num_slabs; ++i) {
        slabs[i].init(slab_sizes[i]);
    }
    
    large_allocations = nullptr;
    heap_used.store(0, memory_order::relaxed);
    heap_allocated.store(0, memory_order::relaxed);
}

void* kmalloc(size_t size) noexcept {
    if (size == 0) return nullptr;
    
    size_t alloc_size = size + sizeof(size_t);
    
    if (alloc_size <= 4096) {
        slab_allocator* slab = find_slab(alloc_size);
        if (!slab) return nullptr;
        
        void* ptr = slab->allocate();
        if (!ptr) return nullptr;
        
        *static_cast<size_t*>(ptr) = size;
        heap_used.fetch_add(size, memory_order::relaxed);
        
        return static_cast<uint8_t*>(ptr) + sizeof(size_t);
    } else {
        size_t pages = (alloc_size + pmm::PAGE_SIZE - 1) / pmm::PAGE_SIZE;
        uint64_t virt = vmm::allocate_region(pages);
        if (!virt) return nullptr;
        
        auto* header = reinterpret_cast<large_allocation*>(virt);
        header->next = large_allocations;
        header->prev = nullptr;
        header->size = size;
        header->pages = pages;
        
        if (large_allocations) {
            large_allocations->prev = header;
        }
        large_allocations = header;
        
        heap_used.fetch_add(size, memory_order::relaxed);
        heap_allocated.fetch_add(pages * pmm::PAGE_SIZE, memory_order::relaxed);
        
        return header + 1;
    }
}

void kfree(void* ptr) noexcept {
    if (!ptr) return;
    
    uint64_t addr = reinterpret_cast<uint64_t>(ptr);
    uint64_t page_addr = addr & ~(pmm::PAGE_SIZE - 1);
    
    if (addr - page_addr == sizeof(large_allocation)) {
        auto* header = static_cast<large_allocation*>(ptr) - 1;
        
        for (auto* alloc = large_allocations; alloc; alloc = alloc->next) {
            if (alloc == header) {
                if (header->prev) {
                    header->prev->next = header->next;
                } else {
                    large_allocations = header->next;
                }
                if (header->next) {
                    header->next->prev = header->prev;
                }
                
                heap_used.fetch_add(-header->size, memory_order::relaxed);
                heap_allocated.fetch_add(-header->pages * pmm::PAGE_SIZE, memory_order::relaxed);
                
                vmm::free_region(reinterpret_cast<uint64_t>(header), header->pages);
                return;
            }
        }
    }
    
    uint8_t* real_ptr = static_cast<uint8_t*>(ptr) - sizeof(size_t);
    size_t size = *reinterpret_cast<size_t*>(real_ptr);
    
    heap_used.fetch_add(-size, memory_order::relaxed);
    
    size_t alloc_size = size + sizeof(size_t);
    slab_allocator* slab = find_slab(alloc_size);
    if (slab) {
        slab->free(real_ptr);
    }
}

heap_stats get_stats() noexcept {
    heap_stats stats;
    stats.used = heap_used.load(memory_order::relaxed);
    stats.allocated = heap_allocated.load(memory_order::relaxed);
    
    for (size_t i = 0; i < num_slabs; ++i) {
        stats.slab_free[i] = slabs[i].get_free_objects();
        stats.slab_total[i] = slabs[i].get_total_objects();
    }
    
    return stats;
}

slab_allocator* find_slab(size_t size) noexcept {
    for (size_t i = 0; i < num_slabs; ++i) {
        if (slab_sizes[i] >= size) {
            return &slabs[i];
        }
    }
    return nullptr;
}

} // namespace hft::heap
