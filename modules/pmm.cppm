module;
#include "../include/freestanding/types.hpp"
#include "../include/freestanding/atomic.hpp"
export module hft.pmm;

export namespace hft::pmm {
using namespace hft;

// Constants
constexpr size_t PAGE_SIZE = 4096;
constexpr size_t PAGE_SHIFT = 12;
constexpr size_t ENTRIES_PER_PAGE = PAGE_SIZE / sizeof(uint64_t);

// Memory zone types
enum class zone_type {
    dma,        // 0-16MB for legacy DMA
    normal,     // 16MB-4GB for normal allocations
    high        // Above 4GB
};

// Memory statistics
struct memory_stats {
    atomic<size_t> total_pages;
    atomic<size_t> free_pages;
    atomic<size_t> reserved_pages;
    atomic<size_t> kernel_pages;
};

// Copyable snapshot of memory stats
struct memory_stats_snapshot {
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t reserved_pages;
    uint64_t kernel_pages;
};

// Memory region from multiboot
struct memory_region {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
};

// Bitmap allocator for physical pages
class bitmap_allocator {
private:
    uint64_t* bitmap;
    size_t bitmap_size;
    size_t total_pages;
    size_t first_free_page;
    atomic<size_t> free_pages;
    
public:
    void init(uint64_t* bitmap_addr, size_t num_pages) noexcept {
        bitmap = bitmap_addr;
        total_pages = num_pages;
        bitmap_size = (num_pages + 63) / 64;  // Round up to 64-bit words
        first_free_page = 0;
        free_pages.store(0, memory_order::relaxed);
        
        // Mark all pages as used initially
        for (size_t i = 0; i < bitmap_size; ++i) {
            bitmap[i] = 0xFFFFFFFFFFFFFFFF;
        }
    }
    
    void mark_free(size_t page_num) noexcept {
        if (page_num >= total_pages) return;
        
        size_t idx = page_num / 64;
        size_t bit = page_num % 64;
        
        uint64_t old_val = bitmap[idx];
        uint64_t new_val = old_val & ~(1ULL << bit);
        
        if (old_val != new_val) {
            bitmap[idx] = new_val;
            free_pages.fetch_add(1, memory_order::relaxed);
            
            if (page_num < first_free_page) {
                first_free_page = page_num;
            }
        }
    }
    
    void mark_used(size_t page_num) noexcept {
        if (page_num >= total_pages) return;
        
        size_t idx = page_num / 64;
        size_t bit = page_num % 64;
        
        uint64_t old_val = bitmap[idx];
        uint64_t new_val = old_val | (1ULL << bit);
        
        if (old_val != new_val) {
            bitmap[idx] = new_val;
            free_pages.fetch_add(-1, memory_order::relaxed);
        }
    }
    
    size_t allocate() noexcept {
        // Start searching from the first known free page
        for (size_t i = first_free_page / 64; i < bitmap_size; ++i) {
            uint64_t word = bitmap[i];
            if (word != 0xFFFFFFFFFFFFFFFF) {
                // Find first clear bit using builtin
                int bit = __builtin_ctzll(~word);
                size_t page_num = i * 64 + bit;
                
                if (page_num < total_pages) {
                    mark_used(page_num);
                    
                    // Update first_free_page hint
                    if (page_num == first_free_page) {
                        first_free_page = page_num + 1;
                    }
                    
                    return page_num;
                }
            }
        }
        
        return static_cast<size_t>(-1);  // No free pages
    }
    
    size_t allocate_contiguous(size_t count) noexcept {
        if (count == 0) return static_cast<size_t>(-1);
        if (count == 1) return allocate();
        
        size_t consecutive = 0;
        size_t start_page = 0;
        
        for (size_t page = 0; page < total_pages; ++page) {
            size_t idx = page / 64;
            size_t bit = page % 64;
            
            if ((bitmap[idx] & (1ULL << bit)) == 0) {
                // Page is free
                if (consecutive == 0) {
                    start_page = page;
                }
                consecutive++;
                
                if (consecutive == count) {
                    // Found enough contiguous pages
                    for (size_t i = 0; i < count; ++i) {
                        mark_used(start_page + i);
                    }
                    return start_page;
                }
            } else {
                // Page is used, reset counter
                consecutive = 0;
            }
        }
        
        return static_cast<size_t>(-1);  // Not enough contiguous pages
    }
    
    void free(size_t page_num) noexcept {
        mark_free(page_num);
    }
    
    void free_contiguous(size_t start_page, size_t count) noexcept {
        for (size_t i = 0; i < count; ++i) {
            mark_free(start_page + i);
        }
    }
    
    size_t get_free_pages() const noexcept {
        return free_pages.load(memory_order::relaxed);
    }
    
    size_t get_total_pages() const noexcept {
        return total_pages;
    }
};

// Global physical memory manager state
inline bitmap_allocator zones[3];  // DMA, Normal, High zones
inline memory_stats stats;
inline uint64_t memory_size;
inline uint64_t kernel_start;
inline uint64_t kernel_end;

// Multiboot memory map parsing
void parse_memory_map(void* mmap_addr, uint32_t mmap_length) noexcept {
    auto* entry = static_cast<memory_region*>(mmap_addr);
    auto* end = reinterpret_cast<memory_region*>(
        reinterpret_cast<uint8_t*>(mmap_addr) + mmap_length
    );
    
    memory_size = 0;
    
    while (entry < end) {
        if (entry->type == 1) {  // Available RAM
            uint64_t base = entry->base;
            uint64_t length = entry->length;
            uint64_t end_addr = base + length;
            
            // Track total memory
            if (end_addr > memory_size) {
                memory_size = end_addr;
            }
            
            // Mark pages as free in appropriate zone
            size_t start_page = (base + PAGE_SIZE - 1) / PAGE_SIZE;
            size_t end_page = end_addr / PAGE_SIZE;
            
            for (size_t page = start_page; page < end_page; ++page) {
                uint64_t addr = page * PAGE_SIZE;
                
                // Skip kernel pages
                if (addr >= kernel_start && addr < kernel_end) {
                    continue;
                }
                
                // Determine zone and mark free
                if (addr < 16 * 1024 * 1024) {
                    zones[static_cast<int>(zone_type::dma)].mark_free(page);
                } else if (addr < 4ULL * 1024 * 1024 * 1024) {
                    size_t zone_page = (addr - 16 * 1024 * 1024) / PAGE_SIZE;
                    zones[static_cast<int>(zone_type::normal)].mark_free(zone_page);
                } else {
                    size_t zone_page = (addr - 4ULL * 1024 * 1024 * 1024) / PAGE_SIZE;
                    zones[static_cast<int>(zone_type::high)].mark_free(zone_page);
                }
                
                stats.free_pages.fetch_add(1, memory_order::relaxed);
            }
            
            stats.total_pages.store(end_page, memory_order::relaxed);
        }
        
        // Move to next entry
        entry++;
    }
}

// Initialize physical memory manager
void init(void* mmap_addr, uint32_t mmap_length, 
          uint64_t kernel_phys_start, uint64_t kernel_phys_end) noexcept {
    kernel_start = kernel_phys_start;
    kernel_end = kernel_phys_end;
    
    // Calculate bitmap locations (place after kernel)
    uint64_t bitmap_start = (kernel_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    // Initialize zones
    size_t dma_pages = (16 * 1024 * 1024) / PAGE_SIZE;
    size_t normal_pages = (4ULL * 1024 * 1024 * 1024 - 16 * 1024 * 1024) / PAGE_SIZE;
    size_t high_pages = (16ULL * 1024 * 1024 * 1024 - 4ULL * 1024 * 1024 * 1024) / PAGE_SIZE;
    
    zones[static_cast<int>(zone_type::dma)].init(
        reinterpret_cast<uint64_t*>(bitmap_start), dma_pages
    );
    
    bitmap_start += (dma_pages + 63) / 64 * 8;
    zones[static_cast<int>(zone_type::normal)].init(
        reinterpret_cast<uint64_t*>(bitmap_start), normal_pages
    );
    
    bitmap_start += (normal_pages + 63) / 64 * 8;
    zones[static_cast<int>(zone_type::high)].init(
        reinterpret_cast<uint64_t*>(bitmap_start), high_pages
    );
    
    // Parse memory map and mark free pages
    if (mmap_addr && mmap_length > 0) {
        parse_memory_map(mmap_addr, mmap_length);
    }
}

// Fallback init when no memory map is available
void init_fallback(uint64_t kernel_phys_start, uint64_t kernel_phys_end,
                   uint64_t total_mem) noexcept {
    kernel_start = kernel_phys_start;
    kernel_end = kernel_phys_end;
    memory_size = total_mem;
    
    // Calculate bitmap locations (place after kernel)
    uint64_t bitmap_start = (kernel_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    // Initialize zones
    size_t dma_pages = (16 * 1024 * 1024) / PAGE_SIZE;
    size_t normal_pages = (4ULL * 1024 * 1024 * 1024 - 16 * 1024 * 1024) / PAGE_SIZE;
    size_t high_pages = (16ULL * 1024 * 1024 * 1024 - 4ULL * 1024 * 1024 * 1024) / PAGE_SIZE;
    
    zones[static_cast<int>(zone_type::dma)].init(
        reinterpret_cast<uint64_t*>(bitmap_start), dma_pages
    );
    
    bitmap_start += (dma_pages + 63) / 64 * 8;
    zones[static_cast<int>(zone_type::normal)].init(
        reinterpret_cast<uint64_t*>(bitmap_start), normal_pages
    );
    
    bitmap_start += (normal_pages + 63) / 64 * 8;
    zones[static_cast<int>(zone_type::high)].init(
        reinterpret_cast<uint64_t*>(bitmap_start), high_pages
    );
    
    // Calculate bitmap end
    uint64_t bitmap_end = bitmap_start + (high_pages + 63) / 64 * 8;
    
    // Mark 16MB-total_mem as free (excluding kernel and bitmaps)
    for (uint64_t addr = 16 * 1024 * 1024; addr < total_mem; addr += PAGE_SIZE) {
        // Skip kernel
        if (addr >= kernel_start && addr < kernel_end) {
            continue;
        }
        
        // Skip bitmap area (starts right after kernel, page-aligned)
        uint64_t bitmap_phys_start = (kernel_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        if (addr >= bitmap_phys_start && addr < bitmap_end) {
            continue;
        }
        
        // Mark page as free in normal zone
        size_t zone_page = (addr - 16 * 1024 * 1024) / PAGE_SIZE;
        zones[static_cast<int>(zone_type::normal)].mark_free(zone_page);
        stats.free_pages.fetch_add(1, memory_order::relaxed);
    }
    
    stats.total_pages.store(total_mem / PAGE_SIZE, memory_order::relaxed);
}

// Allocate a physical page
uint64_t allocate_page(zone_type zone = zone_type::normal) noexcept {
    size_t page_num = zones[static_cast<int>(zone)].allocate();
    
    if (page_num == static_cast<size_t>(-1)) {
        // Try other zones
        if (zone != zone_type::high) {
            page_num = zones[static_cast<int>(zone_type::high)].allocate();
            if (page_num != static_cast<size_t>(-1)) {
                zone = zone_type::high;
            }
        }
        if (page_num == static_cast<size_t>(-1) && zone != zone_type::dma) {
            page_num = zones[static_cast<int>(zone_type::dma)].allocate();
            if (page_num != static_cast<size_t>(-1)) {
                zone = zone_type::dma;
            }
        }
    }
    
    if (page_num != static_cast<size_t>(-1)) {
        stats.free_pages.fetch_add(-1, memory_order::relaxed);
        
        // Convert page number to physical address based on zone
        if (zone == zone_type::dma) {
            return page_num * PAGE_SIZE;
        } else if (zone == zone_type::normal) {
            return 16 * 1024 * 1024 + page_num * PAGE_SIZE;
        } else {
            return 4ULL * 1024 * 1024 * 1024 + page_num * PAGE_SIZE;
        }
    }
    
    return 0;  // Allocation failed
}

// Allocate contiguous physical pages
uint64_t allocate_pages(size_t count, zone_type zone = zone_type::normal) noexcept {
    size_t page_num = zones[static_cast<int>(zone)].allocate_contiguous(count);
    
    if (page_num != static_cast<size_t>(-1)) {
        stats.free_pages.fetch_add(-count, memory_order::relaxed);
        
        // Convert page number to physical address
        if (zone == zone_type::dma) {
            return page_num * PAGE_SIZE;
        } else if (zone == zone_type::normal) {
            return 16 * 1024 * 1024 + page_num * PAGE_SIZE;
        } else {
            return 4ULL * 1024 * 1024 * 1024 + page_num * PAGE_SIZE;
        }
    }
    
    return 0;  // Allocation failed
}

// Free a physical page
void free_page(uint64_t addr) noexcept {
    if (addr == 0) return;
    
    size_t page_num;
    zone_type zone;
    
    if (addr < 16 * 1024 * 1024) {
        zone = zone_type::dma;
        page_num = addr / PAGE_SIZE;
    } else if (addr < 4ULL * 1024 * 1024 * 1024) {
        zone = zone_type::normal;
        page_num = (addr - 16 * 1024 * 1024) / PAGE_SIZE;
    } else {
        zone = zone_type::high;
        page_num = (addr - 4ULL * 1024 * 1024 * 1024) / PAGE_SIZE;
    }
    
    zones[static_cast<int>(zone)].free(page_num);
    stats.free_pages.fetch_add(1, memory_order::relaxed);
}

// Free contiguous physical pages
void free_pages(uint64_t addr, size_t count) noexcept {
    for (size_t i = 0; i < count; ++i) {
        free_page(addr + i * PAGE_SIZE);
    }
}

// Get memory statistics
memory_stats_snapshot get_stats() noexcept {
    return {
        stats.total_pages.load(memory_order::relaxed),
        stats.free_pages.load(memory_order::relaxed),
        stats.reserved_pages.load(memory_order::relaxed),
        stats.kernel_pages.load(memory_order::relaxed)
    };
}

// Get total memory size
uint64_t get_memory_size() noexcept {
    return memory_size;
}

} // namespace hft::pmm
