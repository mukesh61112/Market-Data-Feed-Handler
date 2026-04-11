#ifndef SYMBOL_CACHE_H
#define SYMBOL_CACHE_H

#include <atomic>
#include <cstdint>
#include <vector>
#include <cstddef>
struct MarketState {
    double best_bid;
    double best_ask;
    uint32_t bid_quantity;
    uint32_t ask_quantity;
    double last_traded_price;
    uint32_t last_traded_quantity;
    uint64_t last_update_time;
    uint64_t update_count;
};

class SymbolCache {
public:
    SymbolCache(size_t num_symbols = 100);

    // writer APIs
    void updateBid(uint16_t symbolId, double price, uint32_t qty, uint64_t ts);
    void updateAsk(uint16_t symbolId, double price, uint32_t qty, uint64_t ts);
    void updateTrade(uint16_t symbolId, double price, uint32_t qty, uint64_t ts);

    // reader API (lock-free snapshot)
    MarketState getSnapshot(uint16_t symbolId) const;

private:
    struct alignas(64) AtomicMarketState {
        std::atomic<double> best_bid;
        std::atomic<double> best_ask;
        std::atomic<uint32_t> bid_quantity;
        std::atomic<uint32_t> ask_quantity;
        std::atomic<double> last_traded_price;
        std::atomic<uint32_t> last_traded_quantity;
        std::atomic<uint64_t> last_update_time;
        std::atomic<uint64_t> update_count;
    };

    std::vector<AtomicMarketState> states_;
};

#endif