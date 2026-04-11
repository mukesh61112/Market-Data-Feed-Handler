#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// cache.h  –  Lock-Free Symbol Cache
//
// Seqlock per slot: odd seq = write in progress, even = stable.
// slots_ on HEAP (32 KB) — safe whether cache is a member or local var.
// ─────────────────────────────────────────────────────────────────────────────
#include <atomic>
#include <memory>
#include <cstdint>
#include <cstring>

constexpr size_t MAX_SYMBOLS = 512;

struct MarketState {
    double   best_bid            = 0.0;
    double   best_ask            = 0.0;
    uint32_t bid_quantity        = 0;
    uint32_t ask_quantity        = 0;
    double   last_traded_price   = 0.0;
    uint32_t last_traded_qty     = 0;
    uint64_t last_update_time    = 0;
    uint64_t update_count        = 0;
};

struct alignas(64) CacheSlot {
    std::atomic<uint64_t> seq{0};
    MarketState           state{};
};

class SymbolCache {
public:
    SymbolCache() : slots_(std::make_unique<CacheSlot[]>(MAX_SYMBOLS)) {}

    // ── Writer (single writer thread) ───────────────────────────────────────
    void updateBid(uint16_t sym, double price, uint32_t qty, uint64_t ts) {
        auto& slot = slots_[sym];
        uint64_t s = slot.seq.load(std::memory_order_relaxed);
        slot.seq.store(s + 1, std::memory_order_release);
        std::atomic_thread_fence(std::memory_order_release);
        slot.state.best_bid         = price;
        slot.state.bid_quantity     = qty;
        slot.state.last_update_time = ts;
        ++slot.state.update_count;
        std::atomic_thread_fence(std::memory_order_release);
        slot.seq.store(s + 2, std::memory_order_release);
    }

    void updateAsk(uint16_t sym, double price, uint32_t qty, uint64_t ts) {
        auto& slot = slots_[sym];
        uint64_t s = slot.seq.load(std::memory_order_relaxed);
        slot.seq.store(s + 1, std::memory_order_release);
        std::atomic_thread_fence(std::memory_order_release);
        slot.state.best_ask         = price;
        slot.state.ask_quantity     = qty;
        slot.state.last_update_time = ts;
        ++slot.state.update_count;
        std::atomic_thread_fence(std::memory_order_release);
        slot.seq.store(s + 2, std::memory_order_release);
    }

    void updateTrade(uint16_t sym, double price, uint32_t qty, uint64_t ts) {
        auto& slot = slots_[sym];
        uint64_t s = slot.seq.load(std::memory_order_relaxed);
        slot.seq.store(s + 1, std::memory_order_release);
        std::atomic_thread_fence(std::memory_order_release);
        slot.state.last_traded_price = price;
        slot.state.last_traded_qty   = qty;
        slot.state.last_update_time  = ts;
        ++slot.state.update_count;
        std::atomic_thread_fence(std::memory_order_release);
        slot.seq.store(s + 2, std::memory_order_release);
    }

    // ── Reader (lock-free, any thread) ───────────────────────────────────────
    MarketState getSnapshot(uint16_t sym) const {
        const auto& slot = slots_[sym];
        MarketState snap{};
        uint64_t s1, s2 = 0;
        do {
            s1 = slot.seq.load(std::memory_order_acquire);
            if (s1 & 1) continue;   // write in progress — spin
            std::atomic_thread_fence(std::memory_order_acquire);
            snap = slot.state;
            std::atomic_thread_fence(std::memory_order_acquire);
            s2 = slot.seq.load(std::memory_order_acquire);
        } while (s1 != s2);
        return snap;
    }

private:
    std::unique_ptr<CacheSlot[]> slots_;
};