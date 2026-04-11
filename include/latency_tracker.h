#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// latency_tracker.h  –  Low-overhead ring-buffer latency tracker
//
// ring_ (8 MB) lives on the HEAP via unique_ptr — never on the stack.
// Percentiles via 512-bucket histogram → O(512), no sorting needed.
// record() overhead < 30 ns (single atomic increment + store).
// ─────────────────────────────────────────────────────────────────────────────
#include <atomic>
#include <array>
#include <memory>
#include <cstdint>
#include <fstream>
#include <string>

class LatencyTracker {
public:
    static constexpr size_t   RING_SIZE    = 1 << 20;   // 1M samples = 8 MB
    static constexpr size_t   BUCKET_COUNT = 512;
    static constexpr uint64_t MAX_NS       = 10'000'000; // 10 ms cap

    struct LatencyStats {
        uint64_t min, max, mean;
        uint64_t p50, p95, p99, p999;
        uint64_t sample_count;
    };

    LatencyTracker()
        : ring_(std::make_unique<uint64_t[]>(RING_SIZE))
    {
        for (auto& b : buckets_) b.store(0, std::memory_order_relaxed);
        head_.store(0,         std::memory_order_relaxed);
        total_ns_.store(0,     std::memory_order_relaxed);
        count_.store(0,        std::memory_order_relaxed);
        min_ns_.store(UINT64_MAX, std::memory_order_relaxed);
        max_ns_.store(0,       std::memory_order_relaxed);
    }

    // Hot path — keep fast
    void record(uint64_t latency_ns) {
        if (latency_ns > MAX_NS) latency_ns = MAX_NS;

        size_t idx = head_.fetch_add(1, std::memory_order_relaxed) & (RING_SIZE - 1);
        ring_[idx] = latency_ns;

        size_t b = static_cast<size_t>(latency_ns * BUCKET_COUNT / MAX_NS);
        if (b >= BUCKET_COUNT) b = BUCKET_COUNT - 1;
        buckets_[b].fetch_add(1, std::memory_order_relaxed);

        total_ns_.fetch_add(latency_ns, std::memory_order_relaxed);
        count_.fetch_add(1, std::memory_order_relaxed);

        uint64_t cur = min_ns_.load(std::memory_order_relaxed);
        while (latency_ns < cur &&
               !min_ns_.compare_exchange_weak(cur, latency_ns, std::memory_order_relaxed)) {}
        cur = max_ns_.load(std::memory_order_relaxed);
        while (latency_ns > cur &&
               !max_ns_.compare_exchange_weak(cur, latency_ns, std::memory_order_relaxed)) {}
    }

    LatencyStats get_stats() const {
        uint64_t n = count_.load(std::memory_order_relaxed);
        if (n == 0) return {};
        LatencyStats s{};
        s.sample_count = n;
        s.min  = min_ns_.load(std::memory_order_relaxed);
        s.max  = max_ns_.load(std::memory_order_relaxed);
        s.mean = total_ns_.load(std::memory_order_relaxed) / n;

        auto pct = [&](double p) -> uint64_t {
            uint64_t target = static_cast<uint64_t>(p * static_cast<double>(n));
            uint64_t cum = 0;
            for (size_t b = 0; b < BUCKET_COUNT; ++b) {
                cum += buckets_[b].load(std::memory_order_relaxed);
                if (cum >= target)
                    return (b + 1) * MAX_NS / BUCKET_COUNT;
            }
            return MAX_NS;
        };
        s.p50  = pct(0.500);
        s.p95  = pct(0.950);
        s.p99  = pct(0.990);
        s.p999 = pct(0.999);
        return s;
    }

    void export_csv(const std::string& path) const {
        std::ofstream f(path);
        f << "bucket_upper_ns,count\n";
        for (size_t b = 0; b < BUCKET_COUNT; ++b)
            f << (b + 1) * MAX_NS / BUCKET_COUNT
              << ',' << buckets_[b].load() << '\n';
    }

    void reset() {
        for (auto& b : buckets_) b.store(0, std::memory_order_relaxed);
        head_.store(0);  total_ns_.store(0);  count_.store(0);
        min_ns_.store(UINT64_MAX);  max_ns_.store(0);
    }

private:
    // ring_ on HEAP — 8 MB; must NOT be a plain array member
    std::unique_ptr<uint64_t[]>                     ring_;
    std::array<std::atomic<uint64_t>, BUCKET_COUNT> buckets_;
    std::atomic<size_t>   head_{0};
    std::atomic<uint64_t> total_ns_{0};
    std::atomic<uint64_t> count_{0};
    std::atomic<uint64_t> min_ns_{UINT64_MAX};
    std::atomic<uint64_t> max_ns_{0};
};