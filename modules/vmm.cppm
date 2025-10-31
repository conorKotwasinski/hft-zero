module;

#include "../include/freestanding/types.hpp"
#include "../include/freestanding/atomic.hpp"

export module hft.vmm;
import hft.pmm;

export namespace hft::vmm {

using namespace hft;

// Page table entry flags
enum page_flags : uint64_t {
    present = 1 << 0,
    writable = 1 << 1,
    user = 1 << 2,
    write_through = 1 << 3,
    cache_disable = 1 << 4,
    accessed = 1 << 5,
    dirty = 1 << 6,
    large = 1 << 7,
    global = 1 << 8,
    no_execute = 1ULL << 63
};

// Virtual address breakdown for 4-level paging
struct virtual_address {
    union {
        uint64_t value;
        struct {
            uint64_t offset : 12;
            uint64_t pt_index : 9;
            uint64_t pd_index : 9;
            uint64_t pdpt_index : 9;
            uint64_t pml4_index : 9;
            uint64_t sign_extend : 16;
        };
    };
    
    constexpr virtual_address(uint64_t addr) : value(addr) {}
};

// Page table structure
struct [[gnu::aligned(4096)]] page_table {
    uint64_t entries[512];
    
    void clear() noexcept {
        for (int i = 0; i < 512; ++i) {
            entries[i] = 0;
        }
    }
    
    uint64_t& operator[](size_t idx) noexcept {
        return entries[idx];
    }
    
    const uint64_t& operator[](size_t idx) const noexcept {
        return entries[idx];
    }
};

// Address space structure
class address_space {
private:
    page_table* pml4;
    atomic<size_t> mapped_pages;
    
public:
    address_space() : pml4(nullptr), mapped_pages(0) {}
    
    bool init() noexcept {
        // Allocate PML4 table
        uint64_t phys = pmm::allocate_page();
        if (phys == 0) return false;
        
        pml4 = reinterpret_cast<page_table*>(phys);
        pml4->clear();
        
        // Map higher half kernel space (shared across all address spaces)
        // This will be filled by copying from kernel's PML4
        
        return true;
    }
    
    void destroy() noexcept {
        if (!pml4) return;
        
        // Walk page tables and free all allocated pages
        for (int i = 0; i < 256; ++i) {  // Only user space (lower half)
            if (pml4->entries[i] & present) {
                free_pdpt(pml4->entries[i] & ~0xFFF);
            }
        }
        
        pmm::free_page(reinterpret_cast<uint64_t>(pml4));
        pml4 = nullptr;
    }
    
    bool map(uint64_t virt, uint64_t phys, uint64_t flags) noexcept {
        virtual_address va(virt);
        
        // Get or create PDPT
        page_table* pdpt = get_or_create_table(pml4, va.pml4_index);
        if (!pdpt) return false;
        
        // Get or create PDT
        page_table* pdt = get_or_create_table(pdpt, va.pdpt_index);
        if (!pdt) return false;
        
        // Get or create PT
        page_table* pt = get_or_create_table(pdt, va.pd_index);
        if (!pt) return false;
        
        // Set the page table entry
        pt->entries[va.pt_index] = phys | flags | present;
        mapped_pages.fetch_add(1, memory_order::relaxed);
        
        // Invalidate TLB for this address
        invlpg(virt);
        
        return true;
    }
    
    bool unmap(uint64_t virt) noexcept {
        virtual_address va(virt);
        
        // Navigate to page table
        if (!(pml4->entries[va.pml4_index] & present)) return false;
        
        auto* pdpt = reinterpret_cast<page_table*>(
            pml4->entries[va.pml4_index] & ~0xFFF
        );
        
        if (!(pdpt->entries[va.pdpt_index] & present)) return false;
        
        auto* pdt = reinterpret_cast<page_table*>(
            pdpt->entries[va.pdpt_index] & ~0xFFF
        );
        
        if (!(pdt->entries[va.pd_index] & present)) return false;
        
        auto* pt = reinterpret_cast<page_table*>(
            pdt->entries[va.pd_index] & ~0xFFF
        );
        
        if (!(pt->entries[va.pt_index] & present)) return false;
        
        // Clear the entry
        pt->entries[va.pt_index] = 0;
        mapped_pages.fetch_add(-1, memory_order::relaxed);
        
        // Invalidate TLB
        invlpg(virt);
        
        return true;
    }
    
    uint64_t get_physical(uint64_t virt) const noexcept {
        virtual_address va(virt);
        
        if (!(pml4->entries[va.pml4_index] & present)) return 0;
        
        auto* pdpt = reinterpret_cast<page_table*>(
            pml4->entries[va.pml4_index] & ~0xFFF
        );
        
        if (!(pdpt->entries[va.pdpt_index] & present)) return 0;
        
        // Check for 1GB pages
        if (pdpt->entries[va.pdpt_index] & large) {
            return (pdpt->entries[va.pdpt_index] & ~0x3FFFFFFF) | 
                   (virt & 0x3FFFFFFF);
        }
        
        auto* pdt = reinterpret_cast<page_table*>(
            pdpt->entries[va.pdpt_index] & ~0xFFF
        );
        
        if (!(pdt->entries[va.pd_index] & present)) return 0;
        
        // Check for 2MB pages
        if (pdt->entries[va.pd_index] & large) {
            return (pdt->entries[va.pd_index] & ~0x1FFFFF) | 
                   (virt & 0x1FFFFF);
        }
        
        auto* pt = reinterpret_cast<page_table*>(
            pdt->entries[va.pd_index] & ~0xFFF
        );
        
        if (!(pt->entries[va.pt_index] & present)) return 0;
        
        return (pt->entries[va.pt_index] & ~0xFFF) | (virt & 0xFFF);
    }
    
    void load() const noexcept {
        asm volatile("movq %0, %%cr3" : : "r"(pml4) : "memory");
    }
    
    page_table* get_pml4() noexcept { return pml4; }
    
private:
    page_table* get_or_create_table(page_table* parent, size_t index) noexcept {
        if (parent->entries[index] & present) {
            return reinterpret_cast<page_table*>(
                parent->entries[index] & ~0xFFF
            );
        }
        
        // Allocate new table
        uint64_t phys = pmm::allocate_page();
        if (phys == 0) return nullptr;
        
        auto* table = reinterpret_cast<page_table*>(phys);
        table->clear();
        
        // Set parent entry
        parent->entries[index] = phys | present | writable | user;
        
        return table;
    }
    
    void free_pdpt(uint64_t pdpt_phys) noexcept {
        auto* pdpt = reinterpret_cast<page_table*>(pdpt_phys);
        
        for (int i = 0; i < 512; ++i) {
            if (pdpt->entries[i] & present) {
                free_pdt(pdpt->entries[i] & ~0xFFF);
            }
        }
        
        pmm::free_page(pdpt_phys);
    }
    
    void free_pdt(uint64_t pdt_phys) noexcept {
        auto* pdt = reinterpret_cast<page_table*>(pdt_phys);
        
        for (int i = 0; i < 512; ++i) {
            if (pdt->entries[i] & present) {
                free_pt(pdt->entries[i] & ~0xFFF);
            }
        }
        
        pmm::free_page(pdt_phys);
    }
    
    void free_pt(uint64_t pt_phys) noexcept {
        pmm::free_page(pt_phys);
    }
    
    void invlpg(uint64_t addr) noexcept {
        asm volatile("invlpg (%0)" : : "r"(addr) : "memory");
    }
};

// Global kernel address space
inline address_space kernel_space;

// Higher-half kernel mapping constants
constexpr uint64_t KERNEL_VIRT_BASE = 0xFFFFFFFF80000000;
constexpr uint64_t KERNEL_PHYS_BASE = 0x0;
constexpr uint64_t KERNEL_SIZE = 1ULL * 1024 * 1024 * 1024;  // 1GB

// Initialize virtual memory manager
void init() noexcept {
    // Initialize kernel address space
    kernel_space.init();
    
    // Map the kernel (1GB identity mapped at higher half)
    for (uint64_t offset = 0; offset < KERNEL_SIZE; offset += pmm::PAGE_SIZE) {
        kernel_space.map(
            KERNEL_VIRT_BASE + offset,
            KERNEL_PHYS_BASE + offset,
            present | writable | global
        );
    }
    
    // Load kernel address space
    kernel_space.load();
}

// Static pool for address spaces
inline address_space space_pool[16];
inline size_t next_space = 0;

// Create new address space (for user processes)
address_space* create_address_space() noexcept {
    // Use a simple static pool instead of heap allocation
    
    if (next_space >= 16) return nullptr;
    
    auto* space = &space_pool[next_space++];
    if (!space->init()) {
        next_space--;
        return nullptr;
    }
    
    // Copy kernel mappings (PML4 entries 256-511)
    auto* kernel_pml4 = kernel_space.get_pml4();
    auto* new_pml4 = space->get_pml4();
    
    for (int i = 256; i < 512; ++i) {
        new_pml4->entries[i] = kernel_pml4->entries[i];
    }
    
    return space;
}

// Destroy address space
void destroy_address_space(address_space* space) noexcept {
    if (space && space != &kernel_space) {
        space->destroy();
        // Note: we don't actually free from the pool
    }
}

// Map pages in current address space
bool map(uint64_t virt, uint64_t phys, uint64_t flags) noexcept {
    return kernel_space.map(virt, phys, flags);
}

// Unmap pages in current address space
bool unmap(uint64_t virt) noexcept {
    return kernel_space.unmap(virt);
}

// Get physical address for virtual address
uint64_t get_physical(uint64_t virt) noexcept {
    return kernel_space.get_physical(virt);
}

// Switch address space
void switch_address_space(address_space* space) noexcept {
    if (space) {
        space->load();
    }
}

// Allocate virtual memory region
uint64_t allocate_region(size_t pages, uint64_t flags = present | writable) noexcept {
    // Simple allocation: Find free virtual address range
    // For now, use a static counter
    static uint64_t next_virt = 0x10000000;  // Start at 256MB
    
    uint64_t virt = next_virt;
    next_virt += pages * pmm::PAGE_SIZE;
    
    // Map each page
    for (size_t i = 0; i < pages; ++i) {
        uint64_t phys = pmm::allocate_page();
        if (phys == 0) {
            // Failed to allocate, unmap what we've mapped
            for (size_t j = 0; j < i; ++j) {
                uint64_t v = virt + j * pmm::PAGE_SIZE;
                uint64_t p = get_physical(v);
                unmap(v);
                pmm::free_page(p);
            }
            return 0;
        }
        
        if (!map(virt + i * pmm::PAGE_SIZE, phys, flags)) {
            // Failed to map, clean up
            pmm::free_page(phys);
            for (size_t j = 0; j < i; ++j) {
                uint64_t v = virt + j * pmm::PAGE_SIZE;
                uint64_t p = get_physical(v);
                unmap(v);
                pmm::free_page(p);
            }
            return 0;
        }
    }
    
    return virt;
}

// Free virtual memory region
void free_region(uint64_t virt, size_t pages) noexcept {
    for (size_t i = 0; i < pages; ++i) {
        uint64_t v = virt + i * pmm::PAGE_SIZE;
        uint64_t p = get_physical(v);
        
        if (p) {
            unmap(v);
            pmm::free_page(p);
        }
    }
}

} // namespace hft::vmm
