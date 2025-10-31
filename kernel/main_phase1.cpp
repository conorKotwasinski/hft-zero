#include "../include/freestanding/types.hpp"

import hft.core;
import hft.gdt;
import hft.idt;
import hft.pmm;
import hft.vmm;
import hft.heap;
import hft.trading;
import hft.concurrent;

namespace hft {

// Serial port driver for debugging
namespace serial {
    constexpr uint16_t COM1 = 0x3F8;
    
    void init() noexcept {
        auto outb = [](uint16_t port, uint8_t val) {
            asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
        };
        
        outb(COM1 + 1, 0x00);
        outb(COM1 + 3, 0x80);
        outb(COM1 + 0, 0x03);
        outb(COM1 + 1, 0x00);
        outb(COM1 + 3, 0x03);
        outb(COM1 + 2, 0xC7);
        outb(COM1 + 4, 0x0B);
    }
    
    void putc(char c) noexcept {
        auto inb = [](uint16_t port) -> uint8_t {
            uint8_t ret;
            asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
            return ret;
        };
        
        auto outb = [](uint16_t port, uint8_t val) {
            asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
        };
        
        while ((inb(COM1 + 5) & 0x20) == 0) {
            asm volatile("pause");
        }
        
        outb(COM1, c);
    }
    
    void puts(const char* str) noexcept {
        while (*str) {
            if (*str == '\n') {
                putc('\r');
            }
            putc(*str++);
        }
    }
    
    void put_number(int val) noexcept {
        if (val < 0) {
            putc('-');
            val = -val;
        }
        
        char buf[32];
        int i = 0;
        
        if (val == 0) {
            putc('0');
            return;
        }
        
        while (val > 0) {
            buf[i++] = '0' + (val % 10);
            val /= 10;
        }
        
        while (i > 0) {
            putc(buf[--i]);
        }
    }
}

volatile uint64_t ticks = 0;
void timer_handler(idt::cpu_context*) {
    ticks++;
    if (ticks % 100 == 0) {
        serial::puts("Tick ");
        serial::put_number(ticks / 100);
        serial::putc('\n');
    }
}

void init_timer() noexcept {
    uint32_t divisor = 1193180 / 100;
    
    asm volatile("outb %0, %1" : : "a"((uint8_t)0x36), "Nd"((uint16_t)0x43));
    asm volatile("outb %0, %1" : : "a"((uint8_t)(divisor & 0xFF)), "Nd"((uint16_t)0x40));
    asm volatile("outb %0, %1" : : "a"((uint8_t)((divisor >> 8) & 0xFF)), "Nd"((uint16_t)0x40));
    
    idt::register_handler(idt::irq::timer, timer_handler);
    idt::enable_irq(0);
}

struct kernel_state {
    cpu_features features;
    bool initialized = false;
};
alignas(64) static kernel_state g_state;

void kernel::initialize() noexcept {
    g_state.features = cpu_features::detect();
    g_state.initialized = true;
}

[[noreturn]] void kernel::panic(const char* msg) noexcept {
    serial::puts("\n!!! KERNEL PANIC !!!\n");
    serial::puts(msg);
    serial::puts("\n");
    asm volatile("cli");
    while (true) {
        asm volatile("hlt");
    }
}

cpu_features kernel::get_cpu_features() noexcept {
    return g_state.features;
}

cpu_features cpu_features::detect() noexcept {
    cpu_features features{};
    uint32_t eax, ebx, ecx, edx;
    
    asm volatile(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(7), "c"(0)
    );
    
    features.avx512f = (ebx >> 16) & 1;
    features.avx512dq = (ebx >> 17) & 1;
    features.avx512vl = (ebx >> 31) & 1;
    features.tsx = (ebx >> 11) & 1;
    
    return features;
}

} // namespace hft

extern "C" void kernel_main(hft::uint32_t, void*) {
    // Very first thing - send 'K' to serial
    asm volatile(
        "mov $0x3F8, %%dx\n"
        "mov $0x4B, %%al\n"
        "out %%al, (%%dx)\n"
        ::: "%rax", "%rdx"
    );
    
    using namespace hft;
    
    serial::init();
    serial::puts("\n=====================================\n");
    serial::puts("        HFT-Zero Kernel v0.1        \n");
    serial::puts("=====================================\n\n");
    
    serial::puts("[*] Initializing GDT... ");
    gdt::init();
    serial::puts("[OK]\n");
    
    serial::puts("[*] Initializing IDT... ");
    idt::init();
    serial::puts("[OK]\n");
    
    serial::puts("[*] Initializing PMM... ");
    pmm::init(nullptr, 0, 0x100000, 0x400000);
    serial::puts("[OK]\n");
    
    serial::puts("[*] Initializing VMM... ");
    vmm::init();
    serial::puts("[OK]\n");
    
    serial::puts("[*] Initializing heap... ");
    heap::init();
    serial::puts("[OK]\n");
    
    serial::puts("[*] Initializing timer... ");
    init_timer();
    serial::puts("[OK]\n");
    
    serial::puts("[*] Enabling interrupts... ");
    idt::enable();
    serial::puts("[OK]\n");
    
    serial::puts("[*] Initializing kernel... ");
    kernel::initialize();
    serial::puts("[OK]\n");
    
    serial::puts("\nSystem ready!\n\n");
    
    while (true) {
        asm volatile("hlt");
    }
}

extern "C" [[gnu::used]] hft::uintptr_t __stack_chk_guard = 0xDEADBEEF;

extern "C" [[noreturn]] void __stack_chk_fail() noexcept {
    hft::kernel::panic("Stack overflow");
}
