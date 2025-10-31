#include <cstdint>

// Import our modules
import hft.core;
import hft.trading;
import hft.concurrent;

// Using C++26 #embed for including binary data (P1967R14)
// Uncomment when binary data is needed:
// static constexpr unsigned char config_data[] = {
//     #embed "config.bin"
// };

namespace hft {

// Terminal implementation (can't be in module due to hardware dependency)
namespace terminal {
    volatile std::uint16_t* const vga_buffer = 
        reinterpret_cast<volatile std::uint16_t*>(0xB8000);
    
    void write(const char* str) noexcept {
        static std::size_t pos = 0;
        while (*str) {
            vga_buffer[pos++] = static_cast<std::uint16_t>(*str++) | 0x0F00;
        }
    }
    
    void write_hex(std::uint64_t value) noexcept {
        static const char hex[] = "0123456789ABCDEF";
        char buffer[17] = "0x";
        
        for (int i = 15; i >= 0; --i) {
            buffer[2 + i] = hex[value & 0xF];
            value >>= 4;
        }
        
        write(buffer);
    }
    
    void clear() noexcept {
        for (std::size_t i = 0; i < 80 * 25; ++i) {
            vga_buffer[i] = 0x0F00;
        }
    }
}

// Global kernel state using modules
struct kernel_state {
    cpu_features features;
    trading::order_book<64> book;
    trading::imbalance_signal signal_gen;
    concurrent::spsc_queue<trading::order, 1024> order_queue;
    concurrent::mpsc_queue<trading::execution, 256> execution_queue;
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
    // Clear screen
    terminal::clear();
    
    // Initialize CPU features
    g_state.features = cpu_features::detect();
    
    terminal::write("HFT-Zero Kernel v0.1\n");
    terminal::write("====================\n\n");
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
    terminal::write("Concurrent queues ready\n");
    
    g_state.initialized = true;
}

[[noreturn]] void kernel::panic(const char* msg) noexcept {
    terminal::write("\n!!! KERNEL PANIC !!!\n");
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

// Market data processing loop (simulated)
void process_market_data() noexcept {
    auto& book = g_state.book;
    auto& signal = g_state.signal_gen;
    auto& order_queue = g_state.order_queue;
    
    // Simulate market data updates
    static std::uint64_t tick = 0;
    ++tick;
    
    // Update book with pseudo-random data (simple LFSR)
    price_t base_price = 100'000;
    quantity_t base_qty = 1000;
    
    book.update_bid(base_price + (tick % 10) * 10, base_qty + (tick % 100));
    book.update_ask(base_price + 10 + (tick % 10) * 10, base_qty + (tick % 100));
    
    // Generate trading signal
    auto sig = signal.generate(book);
    
    // Submit order based on signal
    if (sig == trading::imbalance_signal::signal::strong_buy) {
        trading::order new_order{
            .id = tick,
            .price = book.get_spread().ask_price,
            .quantity = 100,
            .is_buy = true,
            .timestamp = tick
        };
        order_queue.try_push(new_order);
    }
}

} // namespace hft

// Kernel entry point
extern "C" [[gnu::section(".text.boot")]] 
void kernel_main() noexcept {
    hft::kernel::initialize();
    
    // Initial market data setup
    auto& book = hft::g_state.book;
    
    // Add initial market depth
    book.update_bid(100'000, 1000);
    book.update_bid(99'990, 2000);
    book.update_bid(99'980, 3000);
    
    book.update_ask(100'010, 1500);
    book.update_ask(100'020, 2500);
    book.update_ask(100'030, 3500);
    
    hft::terminal::write("\nMarket data feed started\n");
    hft::terminal::write("Order book sequence: ");
    hft::terminal::write_hex(book.get_sequence());
    hft::terminal::write("\n");
    
    // Using C++26 Oxford comma in variadic templates (P3176R0)
    using test_types = hft::type_list<int, double, float,>;  // Note trailing comma
    
    hft::terminal::write("\nSystem ready - entering main loop\n");
    
    // Main event loop
    std::uint64_t iterations = 0;
    while (true) {
        hft::process_market_data();
        
        // Show status every million iterations
        if (++iterations % 1'000'000 == 0) {
            hft::terminal::write(".");
        }
        
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
