module;

#include "../include/freestanding/types.hpp"

export module hft.idt;
import hft.gdt;

export namespace hft::idt {

using namespace hft;

// Interrupt frame pushed by CPU
struct [[gnu::packed]] interrupt_frame {
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

// Extended interrupt frame with error code
struct [[gnu::packed]] interrupt_frame_error {
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

// Saved CPU context
struct cpu_context {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
    uint64_t int_no, error_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

// IDT entry structure
struct [[gnu::packed]] idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;        // Interrupt stack table
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
};

static_assert(sizeof(idt_entry) == 16);

// IDT pointer structure
struct [[gnu::packed]] idtr {
    uint16_t limit;
    uint64_t base;
};

// Interrupt types
enum gate_type : uint8_t {
    interrupt_gate = 0x8E,  // Present, DPL0, 64-bit interrupt gate
    trap_gate = 0x8F,        // Present, DPL0, 64-bit trap gate
    user_interrupt = 0xEE,   // Present, DPL3, 64-bit interrupt gate
    user_trap = 0xEF         // Present, DPL3, 64-bit trap gate
};

// Exception numbers
enum exception : uint8_t {
    divide_error = 0,
    debug = 1,
    nmi = 2,
    breakpoint = 3,
    overflow = 4,
    bound_range = 5,
    invalid_opcode = 6,
    device_not_available = 7,
    double_fault = 8,
    invalid_tss = 10,
    segment_not_present = 11,
    stack_fault = 12,
    general_protection = 13,
    page_fault = 14,
    x87_fpu = 16,
    alignment_check = 17,
    machine_check = 18,
    simd_exception = 19,
    virtualization = 20,
    control_protection = 21,
    hypervisor_injection = 28,
    vmm_communication = 29,
    security = 30
};

// IRQ numbers (after remapping)
enum irq : uint8_t {
    timer = 32,
    keyboard = 33,
    cascade = 34,
    com2 = 35,
    com1 = 36,
    lpt2 = 37,
    floppy = 38,
    lpt1 = 39,
    rtc = 40,
    free1 = 41,
    free2 = 42,
    free3 = 43,
    mouse = 44,
    fpu_coproc = 45,
    primary_ata = 46,
    secondary_ata = 47
};

// Interrupt handler type
using interrupt_handler = void (*)(cpu_context*);

// Global IDT
alignas(16) inline idt_entry idt[256];
inline idtr idt_pointer;
inline interrupt_handler handlers[256];

// External ISR stubs (defined in assembly)
extern "C" {
    void isr0(); void isr1(); void isr2(); void isr3();
    void isr4(); void isr5(); void isr6(); void isr7();
    void isr8(); void isr9(); void isr10(); void isr11();
    void isr12(); void isr13(); void isr14(); void isr15();
    void isr16(); void isr17(); void isr18(); void isr19();
    void isr20(); void isr21(); void isr22(); void isr23();
    void isr24(); void isr25(); void isr26(); void isr27();
    void isr28(); void isr29(); void isr30(); void isr31();
    
    void irq0(); void irq1(); void irq2(); void irq3();
    void irq4(); void irq5(); void irq6(); void irq7();
    void irq8(); void irq9(); void irq10(); void irq11();
    void irq12(); void irq13(); void irq14(); void irq15();
}

// Set an IDT entry
void set_gate(uint8_t num, uint64_t handler_addr, uint16_t selector, 
              uint8_t type, uint8_t ist = 0) noexcept {
    idt[num].offset_low = handler_addr & 0xFFFF;
    idt[num].selector = selector;
    idt[num].ist = ist & 0x07;
    idt[num].type_attr = type;
    idt[num].offset_mid = (handler_addr >> 16) & 0xFFFF;
    idt[num].offset_high = (handler_addr >> 32) & 0xFFFFFFFF;
    idt[num].reserved = 0;
}

// Register an interrupt handler
void register_handler(uint8_t num, interrupt_handler handler) noexcept {
    handlers[num] = handler;
}

// Common interrupt handler (called from assembly)
extern "C" void interrupt_handler_common(cpu_context* ctx) {
    if (ctx->int_no < 256 && handlers[ctx->int_no]) {
        handlers[ctx->int_no](ctx);
    } else {
        // Unhandled interrupt - panic or log
        // For now, just acknowledge the interrupt
    }
    
    // Send EOI to PIC if this was an IRQ
    if (ctx->int_no >= 32 && ctx->int_no < 48) {
        // Send EOI to master PIC
        asm volatile("movb $0x20, %%al; outb %%al, $0x20" ::: "al");
        
        // Send EOI to slave PIC if necessary
        if (ctx->int_no >= 40) {
            asm volatile("movb $0x20, %%al; outb %%al, $0xA0" ::: "al");
        }
    }
}

// Initialize PIC (8259A)
void init_pic() noexcept {
    // ICW1 - begin initialization
    asm volatile(
        "movb $0x11, %%al\n"
        "outb %%al, $0x20\n"  // Master PIC command
        "outb %%al, $0xA0\n"  // Slave PIC command
        ::: "al"
    );
    
    // ICW2 - remap interrupts
    asm volatile(
        "movb $0x20, %%al\n"  // Master PIC vector offset (32)
        "outb %%al, $0x21\n"
        "movb $0x28, %%al\n"  // Slave PIC vector offset (40)
        "outb %%al, $0xA1\n"
        ::: "al"
    );
    
    // ICW3 - setup cascading
    asm volatile(
        "movb $0x04, %%al\n"  // Tell master PIC about slave at IRQ2
        "outb %%al, $0x21\n"
        "movb $0x02, %%al\n"  // Tell slave PIC its cascade identity
        "outb %%al, $0xA1\n"
        ::: "al"
    );
    
    // ICW4 - environment info
    asm volatile(
        "movb $0x01, %%al\n"  // 8086 mode
        "outb %%al, $0x21\n"
        "outb %%al, $0xA1\n"
        ::: "al"
    );
    
    // Mask all interrupts initially
    asm volatile(
        "movb $0xFF, %%al\n"
        "outb %%al, $0x21\n"
        "outb %%al, $0xA1\n"
        ::: "al"
    );
}

// Enable/disable specific IRQ
void enable_irq(uint8_t irq_num) noexcept {
    uint16_t port;
    uint8_t value;
    
    if (irq_num < 8) {
        port = 0x21;
    } else {
        port = 0xA1;
        irq_num -= 8;
    }
    
    asm volatile(
        "inb %w1, %%al\n"
        : "=a"(value)
        : "Nd"(port)
    );
    
    value &= ~(1 << irq_num);
    
    asm volatile(
        "outb %%al, %w0\n"
        :
        : "Nd"(port), "a"(value)
    );
}

void disable_irq(uint8_t irq_num) noexcept {
    uint16_t port;
    uint8_t value;
    
    if (irq_num < 8) {
        port = 0x21;
    } else {
        port = 0xA1;
        irq_num -= 8;
    }
    
    asm volatile(
        "inb %w1, %%al\n"
        : "=a"(value)
        : "Nd"(port)
    );
    
    value |= (1 << irq_num);
    
    asm volatile(
        "outb %%al, %w0\n"
        :
        : "Nd"(port), "a"(value)
    );
}

// Initialize the IDT
void init() noexcept {
    // Clear all handlers
    memset(handlers, 0, sizeof(handlers));
    
    // Set up exception handlers (0-31)
    set_gate(0, reinterpret_cast<uint64_t>(isr0), gdt::kernel_code, interrupt_gate);
    set_gate(1, reinterpret_cast<uint64_t>(isr1), gdt::kernel_code, interrupt_gate);
    set_gate(2, reinterpret_cast<uint64_t>(isr2), gdt::kernel_code, interrupt_gate);
    set_gate(3, reinterpret_cast<uint64_t>(isr3), gdt::kernel_code, trap_gate);
    set_gate(4, reinterpret_cast<uint64_t>(isr4), gdt::kernel_code, trap_gate);
    set_gate(5, reinterpret_cast<uint64_t>(isr5), gdt::kernel_code, interrupt_gate);
    set_gate(6, reinterpret_cast<uint64_t>(isr6), gdt::kernel_code, interrupt_gate);
    set_gate(7, reinterpret_cast<uint64_t>(isr7), gdt::kernel_code, interrupt_gate);
    set_gate(8, reinterpret_cast<uint64_t>(isr8), gdt::kernel_code, interrupt_gate, 1);  // Double fault uses IST1
    set_gate(9, reinterpret_cast<uint64_t>(isr9), gdt::kernel_code, interrupt_gate);
    set_gate(10, reinterpret_cast<uint64_t>(isr10), gdt::kernel_code, interrupt_gate);
    set_gate(11, reinterpret_cast<uint64_t>(isr11), gdt::kernel_code, interrupt_gate);
    set_gate(12, reinterpret_cast<uint64_t>(isr12), gdt::kernel_code, interrupt_gate);
    set_gate(13, reinterpret_cast<uint64_t>(isr13), gdt::kernel_code, interrupt_gate);
    set_gate(14, reinterpret_cast<uint64_t>(isr14), gdt::kernel_code, interrupt_gate);
    set_gate(15, reinterpret_cast<uint64_t>(isr15), gdt::kernel_code, interrupt_gate);
    set_gate(16, reinterpret_cast<uint64_t>(isr16), gdt::kernel_code, interrupt_gate);
    set_gate(17, reinterpret_cast<uint64_t>(isr17), gdt::kernel_code, interrupt_gate);
    set_gate(18, reinterpret_cast<uint64_t>(isr18), gdt::kernel_code, interrupt_gate);
    set_gate(19, reinterpret_cast<uint64_t>(isr19), gdt::kernel_code, interrupt_gate);
    set_gate(20, reinterpret_cast<uint64_t>(isr20), gdt::kernel_code, interrupt_gate);
    set_gate(21, reinterpret_cast<uint64_t>(isr21), gdt::kernel_code, interrupt_gate);
    // 22-27 reserved
    set_gate(28, reinterpret_cast<uint64_t>(isr28), gdt::kernel_code, interrupt_gate);
    set_gate(29, reinterpret_cast<uint64_t>(isr29), gdt::kernel_code, interrupt_gate);
    set_gate(30, reinterpret_cast<uint64_t>(isr30), gdt::kernel_code, interrupt_gate);
    set_gate(31, reinterpret_cast<uint64_t>(isr31), gdt::kernel_code, interrupt_gate);
    
    // Set up IRQ handlers (32-47)
    set_gate(32, reinterpret_cast<uint64_t>(irq0), gdt::kernel_code, interrupt_gate);
    set_gate(33, reinterpret_cast<uint64_t>(irq1), gdt::kernel_code, interrupt_gate);
    set_gate(34, reinterpret_cast<uint64_t>(irq2), gdt::kernel_code, interrupt_gate);
    set_gate(35, reinterpret_cast<uint64_t>(irq3), gdt::kernel_code, interrupt_gate);
    set_gate(36, reinterpret_cast<uint64_t>(irq4), gdt::kernel_code, interrupt_gate);
    set_gate(37, reinterpret_cast<uint64_t>(irq5), gdt::kernel_code, interrupt_gate);
    set_gate(38, reinterpret_cast<uint64_t>(irq6), gdt::kernel_code, interrupt_gate);
    set_gate(39, reinterpret_cast<uint64_t>(irq7), gdt::kernel_code, interrupt_gate);
    set_gate(40, reinterpret_cast<uint64_t>(irq8), gdt::kernel_code, interrupt_gate);
    set_gate(41, reinterpret_cast<uint64_t>(irq9), gdt::kernel_code, interrupt_gate);
    set_gate(42, reinterpret_cast<uint64_t>(irq10), gdt::kernel_code, interrupt_gate);
    set_gate(43, reinterpret_cast<uint64_t>(irq11), gdt::kernel_code, interrupt_gate);
    set_gate(44, reinterpret_cast<uint64_t>(irq12), gdt::kernel_code, interrupt_gate);
    set_gate(45, reinterpret_cast<uint64_t>(irq13), gdt::kernel_code, interrupt_gate);
    set_gate(46, reinterpret_cast<uint64_t>(irq14), gdt::kernel_code, interrupt_gate);
    set_gate(47, reinterpret_cast<uint64_t>(irq15), gdt::kernel_code, interrupt_gate);
    
    // Initialize PIC
    init_pic();
    
    // Load IDT
    idt_pointer.limit = sizeof(idt) - 1;
    idt_pointer.base = reinterpret_cast<uint64_t>(&idt);
    
    asm volatile("lidt %0" : : "m"(idt_pointer));
}

// Enable interrupts
void enable() noexcept {
    asm volatile("sti");
}

// Disable interrupts  
void disable() noexcept {
    asm volatile("cli");
}

// Check if interrupts are enabled
bool enabled() noexcept {
    uint64_t flags;
    asm volatile(
        "pushfq\n"
        "popq %0"
        : "=r"(flags)
    );
    return (flags & 0x200) != 0;
}

} // namespace hft::idt
