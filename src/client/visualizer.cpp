// ─────────────────────────────────────────────────────────────────────────────
// visualizer.cpp  –  Real-time terminal display (ANSI escape codes)
//
// Runs in its own thread; polls the SymbolCache every 500 ms.
// Shows top-20 symbols by update frequency, latency percentiles, etc.
// ─────────────────────────────────────────────────────────────────────────────
#include "../include/visualizer.h"
#include "../include/cache.h"
#include "../include/latency_tracker.h"
#include "../include/parser.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>

// ANSI helpers
#define ANSI_CLEAR     "\033[2J\033[H"
#define ANSI_BOLD      "\033[1m"
#define ANSI_RESET     "\033[0m"
#define ANSI_GREEN     "\033[32m"
#define ANSI_RED       "\033[31m"
#define ANSI_CYAN      "\033[36m"
#define ANSI_YELLOW    "\033[33m"

// ─── Visualizer ──────────────────────────────────────────────────────────────
Visualizer::Visualizer(const SymbolCache& cache,
                        const LatencyTracker& lat,
                        const ParserStats& pstats,
                        size_t num_symbols)
    : cache_(cache), lat_(lat), pstats_(pstats), num_symbols_(num_symbols)
{
    prev_prices_.resize(num_symbols, 0.0);
}

void Visualizer::start() {
    running_.store(true);
    thread_ = std::thread([this]{ render_loop(); });
}

void Visualizer::stop() {
    running_.store(false);
    if (thread_.joinable()) thread_.join();
    // Restore terminal
    printf(ANSI_RESET "\n");
}

void Visualizer::set_message_rate(double rate) {
    msg_rate_.store(static_cast<uint64_t>(rate));
}
void Visualizer::add_message_count(uint64_t n) {
    total_msgs_.fetch_add(n, std::memory_order_relaxed);
}

// ─── Render loop ─────────────────────────────────────────────────────────────
void Visualizer::render_loop() {
    using clock = std::chrono::steady_clock;
    auto next = clock::now();

    while (running_.load()) {
        next += std::chrono::milliseconds(500);
        render_frame();
        std::this_thread::sleep_until(next);
    }
}

static std::string uptime_str(uint64_t secs) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
             secs / 3600, (secs % 3600) / 60, secs % 60);
    return buf;
}

void Visualizer::render_frame() {
    using clock = std::chrono::steady_clock;
    static auto start_time = clock::now();

    // Collect update counts for all symbols → pick top 20
    struct SymEntry { uint16_t id; uint64_t updates; double bid, ask, ltp; };
    std::vector<SymEntry> entries;
    entries.reserve(num_symbols_);
    for (size_t i = 0; i < num_symbols_; ++i) {
        auto s = cache_.getSnapshot(static_cast<uint16_t>(i));
        entries.push_back({ (uint16_t)i, s.update_count,
                            s.best_bid, s.best_ask, s.last_traded_price });
    }
    std::sort(entries.begin(), entries.end(),
              [](const SymEntry& a, const SymEntry& b){
                  return a.updates > b.updates; });

    auto stats  = lat_.get_stats();
    uint64_t up = std::chrono::duration_cast<std::chrono::seconds>(
                      clock::now() - start_time).count();

    // ── Print ────────────────────────────────────────────────────────────────
    printf(ANSI_CLEAR);
    printf(ANSI_BOLD ANSI_CYAN
           "=== NSE Market Data Feed Handler ===\n" ANSI_RESET);
    printf("Connected to: localhost:9876    Uptime: %s\n",
           uptime_str(up).c_str());
    printf("Messages: %llu    Rate: %llu msg/s\n\n",
           (unsigned long long)total_msgs_.load(),
           (unsigned long long)msg_rate_.load());

    // Table header
    printf(ANSI_BOLD
           "%-12s %10s %10s %10s %10s %6s %8s\n" ANSI_RESET,
           "Symbol", "Bid", "Ask", "LTP", "Volume", "Chg%", "Updates");
    printf("%s\n", std::string(70, '-').c_str());

    size_t shown = std::min(entries.size(), (size_t)20);
    for (size_t i = 0; i < shown; ++i) {
        auto& e  = entries[i];
        double prev = prev_prices_[e.id];
        double chg  = (prev > 0.0) ? (e.ltp - prev) / prev * 100.0 : 0.0;
        prev_prices_[e.id] = e.ltp;

        const char* color = (chg >= 0) ? ANSI_GREEN : ANSI_RED;
        char sym[16]; snprintf(sym, sizeof(sym), "SYM%04u", e.id);

        printf("%s%-12s%s %10.2f %10.2f %10.2f %10s %s%+6.2f%%%s %8llu\n",
               color, sym, ANSI_RESET,
               e.bid, e.ask, e.ltp,
               "—",   // volume placeholder
               color, chg, ANSI_RESET,
               (unsigned long long)e.updates);
    }

    printf("\n" ANSI_BOLD "Statistics:\n" ANSI_RESET);
    printf("  Parser Throughput : %llu msg/s\n",
           (unsigned long long)msg_rate_.load());
    printf("  Latency           : p50=%llu us  p99=%llu us  p999=%llu us\n",
           (unsigned long long)(stats.p50  / 1000),
           (unsigned long long)(stats.p99  / 1000),
           (unsigned long long)(stats.p999 / 1000));
    printf("  Sequence Gaps     : %llu\n",
           (unsigned long long)pstats_.seq_gaps);
    printf("  Cache Updates     : %llu\n",
           (unsigned long long)total_msgs_.load());
    printf("\n" ANSI_YELLOW
           "Press 'q' to quit, 'r' to reset stats\n" ANSI_RESET);
    fflush(stdout);
}
