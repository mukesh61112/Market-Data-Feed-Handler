#include "SymbolCache.h"

SymbolCache::SymbolCache(size_t num_symbols) : states_(num_symbols) {
    for (size_t i = 0; i < num_symbols; ++i) {
        states_[i].best_bid.store(0.0);
        states_[i].best_ask.store(0.0);
        states_[i].bid_quantity.store(0);
        states_[i].ask_quantity.store(0);
        states_[i].last_traded_price.store(0.0);
        states_[i].last_traded_quantity.store(0);
        states_[i].last_update_time.store(0);
        states_[i].update_count.store(0);
    }
}

// -------- WRITER --------

void SymbolCache::updateBid(uint16_t id, double price, uint32_t qty, uint64_t ts) {
    auto& s = states_[id];

    s.best_bid.store(price, std::memory_order_relaxed);
    s.bid_quantity.store(qty, std::memory_order_relaxed);
    s.last_update_time.store(ts, std::memory_order_relaxed);
    s.update_count.fetch_add(1, std::memory_order_relaxed);
}

void SymbolCache::updateAsk(uint16_t id, double price, uint32_t qty, uint64_t ts) {
    auto& s = states_[id];

    s.best_ask.store(price, std::memory_order_relaxed);
    s.ask_quantity.store(qty, std::memory_order_relaxed);
    s.last_update_time.store(ts, std::memory_order_relaxed);
    s.update_count.fetch_add(1, std::memory_order_relaxed);
}

void SymbolCache::updateTrade(uint16_t id, double price, uint32_t qty, uint64_t ts) {
    auto& s = states_[id];

    s.last_traded_price.store(price, std::memory_order_relaxed);
    s.last_traded_quantity.store(qty, std::memory_order_relaxed);
    s.last_update_time.store(ts, std::memory_order_relaxed);
    s.update_count.fetch_add(1, std::memory_order_relaxed);
}

// -------- READER (LOCK-FREE SNAPSHOT) --------

MarketState SymbolCache::getSnapshot(uint16_t id) const {
    MarketState snap;
    const auto& s = states_[id];

    // simple consistent read (retry if update_count changes)
    while (true) {
        uint64_t before = s.update_count.load(std::memory_order_acquire);

        snap.best_bid = s.best_bid.load(std::memory_order_relaxed);
        snap.best_ask = s.best_ask.load(std::memory_order_relaxed);
        snap.bid_quantity = s.bid_quantity.load(std::memory_order_relaxed);
        snap.ask_quantity = s.ask_quantity.load(std::memory_order_relaxed);
        snap.last_traded_price = s.last_traded_price.load(std::memory_order_relaxed);
        snap.last_traded_quantity = s.last_traded_quantity.load(std::memory_order_relaxed);
        snap.last_update_time = s.last_update_time.load(std::memory_order_relaxed);

        uint64_t after = s.update_count.load(std::memory_order_acquire);

        if (before == after) {
            snap.update_count = after;
            return snap;
        }
    }
}