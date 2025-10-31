module;

#include "../include/freestanding/types.hpp"
#include "../include/freestanding/atomic.hpp"

export module hft.trading;
import hft.core;

export namespace hft::trading {

using price_t = int64_t;
using quantity_t = uint64_t;
using order_id_t = uint64_t;

// Array implementation (since we don't have std::array)
template<typename T, size_t N>
struct array {
    T data[N];
    
    constexpr T& operator[](size_t idx) noexcept { return data[idx]; }
    constexpr const T& operator[](size_t idx) const noexcept { return data[idx]; }
    
    constexpr T* begin() noexcept { return data; }
    constexpr T* end() noexcept { return data + N; }
    constexpr const T* begin() const noexcept { return data; }
    constexpr const T* end() const noexcept { return data + N; }
};

// Price level in the order book
struct price_level : cache_aligned<price_level> {
    price_t price;
    atomic<quantity_t> bid_quantity;
    atomic<quantity_t> ask_quantity;
    atomic<uint32_t> bid_count;
    atomic<uint32_t> ask_count;
    
    constexpr price_level() noexcept 
        : price{0}, bid_quantity{0}, ask_quantity{0}, 
          bid_count{0}, ask_count{0} {}
};

// Simple lower_bound implementation
template<typename It, typename T, typename Compare>
It lower_bound(It first, It last, const T& value, Compare comp) {
    It it;
    ptrdiff_t count, step;
    count = last - first;
    
    while (count > 0) {
        it = first;
        step = count / 2;
        it += step;
        
        if (comp(*it, value)) {
            first = ++it;
            count -= step + 1;
        } else {
            count = step;
        }
    }
    return first;
}

// Using C++26 structured binding as condition (P0963R3)
template<size_t MaxLevels = 32>
class order_book {
public:
    static constexpr size_t max_levels = MaxLevels;
    
private:
    array<price_level, max_levels> levels_;
    atomic<size_t> bid_depth_{0};
    atomic<size_t> ask_depth_{0};
    atomic<uint64_t> sequence_{0};
    
public:
    // Update bid side
    void update_bid(price_t price, quantity_t quantity) noexcept {
        if (auto [idx, found] = find_or_insert_price(price); found) {
            levels_[idx].bid_quantity.store(quantity, memory_order::relaxed);
        }
        sequence_.fetch_add(1, memory_order::release);
    }
    
    // Update ask side  
    void update_ask(price_t price, quantity_t quantity) noexcept {
        if (auto [idx, found] = find_or_insert_price(price); found) {
            levels_[idx].ask_quantity.store(quantity, memory_order::relaxed);
        }
        sequence_.fetch_add(1, memory_order::release);
    }
    
    // Calculate order imbalance
    [[nodiscard]] double calculate_imbalance(size_t depth = 5) const noexcept {
        double bid_volume = 0.0;
        double ask_volume = 0.0;
        
        for (size_t i = 0; i < depth && i < max_levels; ++i) {
            bid_volume += levels_[i].bid_quantity.load(memory_order::relaxed);
            ask_volume += levels_[i].ask_quantity.load(memory_order::relaxed);
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
            .bid_size = levels_[0].bid_quantity.load(memory_order::acquire),
            .ask_size = levels_[0].ask_quantity.load(memory_order::acquire)
        };
    }
    
    // Get sequence number for tracking updates
    [[nodiscard]] uint64_t get_sequence() const noexcept {
        return sequence_.load(memory_order::acquire);
    }
    
private:
    struct pair {
        size_t first;
        bool second;
    };
    
    pair find_or_insert_price(price_t price) noexcept {
        // Binary search for price level
        auto it = lower_bound(levels_.begin(), levels_.end(), price,
            [](const price_level& level, price_t p) {
                return level.price < p;
            });
        
        if (it != levels_.end() && it->price == price) {
            return {static_cast<size_t>(it - levels_.begin()), true};
        }
        
        // Insert new level if space available
        auto dist = it - levels_.begin();
        if (dist < max_levels) {
            // Shift levels and insert
            for (auto p = levels_.end() - 1; p > it; --p) {
                *p = *(p - 1);
            }
            it->price = price;
            return {static_cast<size_t>(dist), true};
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
    uint64_t timestamp;
};

// Execution report
struct execution {
    order_id_t order_id;
    price_t fill_price;
    quantity_t fill_quantity;
    uint64_t timestamp;
};

} // namespace hft::trading
