module;

#include <atomic>
#include <array>
#include <cstdint>
#include <algorithm>
#include <ranges>

export module hft.trading;
import hft.core;

export namespace hft::trading {

using price_t = std::int64_t;
using quantity_t = std::uint64_t;
using order_id_t = std::uint64_t;

// Price level in the order book
struct price_level : cache_aligned<price_level> {
    price_t price;
    std::atomic<quantity_t> bid_quantity;
    std::atomic<quantity_t> ask_quantity;
    std::atomic<std::uint32_t> bid_count;
    std::atomic<std::uint32_t> ask_count;
    
    constexpr price_level() noexcept 
        : price{0}, bid_quantity{0}, ask_quantity{0}, 
          bid_count{0}, ask_count{0} {}
};

// Using C++26 structured binding as condition (P0963R3)
template<std::size_t MaxLevels = 32>
class order_book {
public:
    static constexpr std::size_t max_levels = MaxLevels;
    
private:
    std::array<price_level, max_levels> levels_;
    std::atomic<std::size_t> bid_depth_{0};
    std::atomic<std::size_t> ask_depth_{0};
    std::atomic<std::uint64_t> sequence_{0};
    
public:
    // Update bid side
    void update_bid(price_t price, quantity_t quantity) noexcept {
        if (auto [idx, found] = find_or_insert_price(price); found) {
            levels_[idx].bid_quantity.store(quantity, std::memory_order_relaxed);
        }
        sequence_.fetch_add(1, std::memory_order_release);
    }
    
    // Update ask side  
    void update_ask(price_t price, quantity_t quantity) noexcept {
        if (auto [idx, found] = find_or_insert_price(price); found) {
            levels_[idx].ask_quantity.store(quantity, std::memory_order_relaxed);
        }
        sequence_.fetch_add(1, std::memory_order_release);
    }
    
    // Calculate order imbalance using SIMD
    [[nodiscard]] double calculate_imbalance(std::size_t depth = 5) const noexcept {
        double bid_volume = 0.0;
        double ask_volume = 0.0;
        
        // Use C++23 views for cleaner iteration
        auto bid_range = levels_ | std::views::take(depth);
        auto ask_range = levels_ | std::views::take(depth);
        
        for (const auto& level : bid_range) {
            bid_volume += level.bid_quantity.load(std::memory_order_relaxed);
        }
        
        for (const auto& level : ask_range) {
            ask_volume += level.ask_quantity.load(std::memory_order_relaxed);
        }
        
        double total = bid_volume + ask_volume;
        return total > 0.0 ? (bid_volume - ask_volume) / total : 0.0;
    }
    
    // Get best bid/ask spread
    [[nodiscard]] constexpr auto get_spread() const noexcept {
        struct spread_info {
            price_t bid_price;
            price_t ask_price;
            quantity_t bid_size;
            quantity_t ask_size;
        };
        
        return spread_info{
            .bid_price = levels_[0].price,
            .ask_price = levels_[0].price,
            .bid_size = levels_[0].bid_quantity.load(std::memory_order_acquire),
            .ask_size = levels_[0].ask_quantity.load(std::memory_order_acquire)
        };
    }
    
    // Get sequence number for tracking updates
    [[nodiscard]] std::uint64_t get_sequence() const noexcept {
        return sequence_.load(std::memory_order_acquire);
    }
    
private:
    std::pair<std::size_t, bool> find_or_insert_price(price_t price) noexcept {
        // Binary search for price level
        auto it = std::lower_bound(levels_.begin(), levels_.end(), price,
            [](const price_level& level, price_t p) {
                return level.price < p;
            });
        
        if (it != levels_.end() && it->price == price) {
            return {std::distance(levels_.begin(), it), true};
        }
        
        // Insert new level if space available
        if (std::distance(levels_.begin(), it) < max_levels) {
            // Shift levels and insert
            std::move_backward(it, levels_.end() - 1, levels_.end());
            it->price = price;
            return {std::distance(levels_.begin(), it), true};
        }
        
        return {0, false};
    }
};

// Order imbalance signal generator
class imbalance_signal {
    static constexpr double threshold = 0.65;
    
public:
    enum class signal { strong_buy, buy, neutral, sell, strong_sell };
    
    template<typename OrderBook>
    [[nodiscard]] signal generate(const OrderBook& book) const noexcept {
        double imbalance = book.calculate_imbalance();
        
        if (imbalance > threshold) return signal::strong_buy;
        if (imbalance > threshold / 2) return signal::buy;
        if (imbalance < -threshold) return signal::strong_sell;
        if (imbalance < -threshold / 2) return signal::sell;
        return signal::neutral;
    }
};

// Order structure for submission
struct order {
    order_id_t id;
    price_t price;
    quantity_t quantity;
    bool is_buy;
    std::uint64_t timestamp;
};

// Execution report
struct execution {
    order_id_t order_id;
    price_t fill_price;
    quantity_t fill_quantity;
    std::uint64_t timestamp;
};

} // namespace hft::trading
