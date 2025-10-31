module;
#include "../include/freestanding/types.hpp"
export module hft.gdt;

export namespace hft::gdt {
using namespace hft;

// GDT entry structure
struct [[gnu::packed]] descriptor {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t flags_limit_high;
    uint8_t base_high;
    uint32_t base_upper;
    uint32_t reserved;
};
static_assert(sizeof(descriptor) == 16);

// GDT pointer structure
struct [[gnu::packed]] gdtr {
    uint16_t limit;
    uint64_t base;
};

// TSS structure for 64-bit
struct [[gnu::packed]] tss {
    uint32_t reserved0;
    uint64_t rsp0;      // Stack pointer for ring 0
    uint64_t rsp1;      // Stack pointer for ring 1
    uint64_t rsp2;      // Stack pointer for ring 2
    uint64_t reserved1;
    uint64_t ist[7];    // Interrupt stack table
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
};

// GDT segments
enum segment : uint16_t {
    null = 0x00,
    kernel_code = 0x08,
    kernel_data = 0x10,
    user_code = 0x18,
    user_data = 0x20,
    tss_segment = 0x28
};

// Global GDT and TSS - CHANGED: static -> inline
alignas(16) inline descriptor gdt[7];
alignas(16) inline tss system_tss;
inline gdtr gdt_pointer;

// Stack for interrupts (16KB) - CHANGED: static -> inline
alignas(16) inline uint8_t interrupt_stack[16384];

// Create a GDT entry
constexpr descriptor make_descriptor(uint64_t base, uint32_t limit, 
                                     uint8_t access, uint8_t flags) {
    descriptor desc{};
    
    // For 64-bit code/data segments, base and limit are ignored
    desc.limit_low = limit & 0xFFFF;
    desc.base_low = base & 0xFFFF;
    desc.base_middle = (base >> 16) & 0xFF;
    desc.access = access;
    desc.flags_limit_high = ((limit >> 16) & 0x0F) | (flags << 4);
    desc.base_high = (base >> 24) & 0xFF;
    desc.base_upper = (base >> 32) & 0xFFFFFFFF;
    desc.reserved = 0;
    
    return desc;
}

// Create a TSS descriptor (special case, spans two GDT entries)
void set_tss_descriptor(uint64_t tss_addr, uint32_t tss_size) {
    // TSS descriptor is 16 bytes (two GDT entries)
    uint64_t base = tss_addr;
    uint32_t limit = tss_size - 1;
    
    // Lower 8 bytes
    gdt[5].limit_low = limit & 0xFFFF;
    gdt[5].base_low = base & 0xFFFF;
    gdt[5].base_middle = (base >> 16) & 0xFF;
    gdt[5].access = 0x89;  // Present, TSS available
    gdt[5].flags_limit_high = ((limit >> 16) & 0x0F) | 0x00;
    gdt[5].base_high = (base >> 24) & 0xFF;
    
    // Upper 8 bytes
    gdt[5].base_upper = (base >> 32) & 0xFFFFFFFF;
    gdt[5].reserved = 0;
}

// Initialize the GDT
void init() noexcept {
    // Null segment
    gdt[0] = make_descriptor(0, 0, 0, 0);
    
    // Kernel code segment (64-bit)
    gdt[1] = make_descriptor(0, 0xFFFFF, 0x9A, 0xA);  // Present, DPL0, Code, Executable, Readable
    
    // Kernel data segment
    gdt[2] = make_descriptor(0, 0xFFFFF, 0x92, 0xC);  // Present, DPL0, Data, Writable
    
    // User code segment (64-bit)
    gdt[3] = make_descriptor(0, 0xFFFFF, 0xFA, 0xA);  // Present, DPL3, Code
    
    // User data segment
    gdt[4] = make_descriptor(0, 0xFFFFF, 0xF2, 0xC);  // Present, DPL3, Data
    
    // Initialize TSS
    memset(&system_tss, 0, sizeof(tss));
    system_tss.rsp0 = reinterpret_cast<uint64_t>(&interrupt_stack[16384]);
    system_tss.iopb_offset = sizeof(tss);
    
    // Set TSS descriptor
    set_tss_descriptor(reinterpret_cast<uint64_t>(&system_tss), sizeof(tss));
    
    // Load GDT
    gdt_pointer.limit = sizeof(gdt) - 1;
    gdt_pointer.base = reinterpret_cast<uint64_t>(&gdt);
    
    asm volatile(
        "lgdt %0\n"
        "pushq %1\n"              // Push code segment
        "leaq 1f(%%rip), %%rax\n" // Load address of label 1
        "pushq %%rax\n"
        "lretq\n"                 // Far return to reload CS
        "1:\n"
        "movw %2, %%ax\n"         // Load data segment
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        "movw %%ax, %%ss\n"
        :
        : "m"(gdt_pointer), "i"(kernel_code), "i"(kernel_data)
        : "rax", "memory"
    );
    
    // Load TSS
    asm volatile(
        "movw %0, %%ax\n"
        "ltr %%ax\n"
        :
        : "i"(tss_segment)
        : "ax"
    );
}

// Set kernel stack for interrupts
void set_kernel_stack(uint64_t stack_ptr) noexcept {
    system_tss.rsp0 = stack_ptr;
}

// Get current code segment
uint16_t get_cs() noexcept {
    uint16_t cs;
    asm volatile("movw %%cs, %0" : "=r"(cs));
    return cs;
}

// Get current data segment
uint16_t get_ds() noexcept {
    uint16_t ds;
    asm volatile("movw %%ds, %0" : "=r"(ds));
    return ds;
}

} // namespace hft::gdt
