#include "kernel/core.hpp"
#include "trading/order_book.hpp"
#include <cstdint>

// Using C++26 #embed for including binary data (P1967R14)
// Uncomment when binary data is needed:
// static constexpr unsigned char config_data[] = {
//     #embed "config.bin"
// };

namespace hft {

// Terminal output for early boot
namespace terminal {
    volatile std::uint16_t* const vga_buffer = 
        reinterpret_cast<volatile std::uint16_t*>(0xB8000);
    
    void write(const char* str) noexcept {
        static std::size_t pos = 0;
        while (*str) {
            vga_buffer[pos++] = static_cast<std::uint16_t>(*str++) | 0x0F00;
        }
    }
}

// Global kernel state
struct kernel_state {
    cpu_features features;
    trading::order_book<64> book;
    trading::imbalance_signal signal_gen;
    bool initialized = false;
};

alignas(64) static kernel_state g_state;

// Using C++26 variadic friends (P2893R3) for access control
template<typename... Friends>
class restricted_access {
    template<typename T>
    friend T;  // Each type in Friends becomes a friend
    
private:
    static void privileged_operation() noexcept {
        terminal::write("Privileged operation executed\n");
    }
};

void kernel::initialize() noexcept {
    // Initialize CPU features
    g_state.features = cpu_features::detect();
    
    terminal::write("HFT-Zero Kernel v0.1\n");
    terminal::write("CPU Features: ");
    
    if (g_state.features.avx512f) {
        terminal::write("AVX-512F ");
    }
    if (g_state.features.tsx) {
        terminal::write("TSX ");
    }
    terminal::write("\n");
    
    // Initialize order book
    terminal::write("Order book initialized\n");
    
    g_state.initialized = true;
}

[[noreturn]] void kernel::panic(const char* msg) noexcept {
    terminal::write("KERNEL PANIC: ");
    terminal::write(msg);
    terminal::write("\n");
    
    // Halt the CPU
    while (true) {
        asm volatile("cli; hlt");
    }
}

cpu_features kernel::get_cpu_features() noexcept {
    return g_state.features;
}

cpu_features cpu_features::detect() noexcept {
    cpu_features features{};
    
    std::uint32_t eax, ebx, ecx, edx;
    
    // Check for AVX-512 support
    asm volatile(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(7), "c"(0)
    );
    
    features.avx512f = (ebx >> 16) & 1;
    features.avx512dq = (ebx >> 17) & 1;
    features.avx512vl = (ebx >> 31) & 1;
    
    // Check for TSX support
    features.tsx = (ebx >> 11) & 1;
    
    return features;
}

} // namespace hft

// Kernel entry point
extern "C" [[gnu::section(".text.boot")]] 
void kernel_main() noexcept {
    hft::kernel::initialize();
    
    // Simulate market data processing
    auto& book = hft::g_state.book;
    auto& signal = hft::g_state.signal_gen;
    
    // Add some test data
    book.update_bid(100'000, 1000);
    book.update_bid(99'990, 2000);
    book.update_ask(100'010, 1500);
    book.update_ask(100'020, 2500);
    
    // Generate trading signal
    auto sig = signal.generate(book);
    
    // Using C++26 Oxford comma in variadic templates (P3176R0)
    // This allows trailing commas in template parameter lists
    using test_types = hft::type_list<int, double, float,>;  // Note trailing comma
    
    hft::terminal::write("System ready\n");
    
    // Main event loop
    while (true) {
        asm volatile("pause");  // CPU hint for spin loops
    }
}

// Stack guard for stack overflow detection
extern "C" [[gnu::used, gnu::section(".stack_guard")]]
std::uintptr_t __stack_chk_guard = 0xDEADBEEFDEADBEEF;

extern "C" [[noreturn]]
void __stack_chk_fail() noexcept {
    hft::kernel::panic("Stack overflow detected");
}
