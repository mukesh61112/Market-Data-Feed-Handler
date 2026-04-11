#pragma once
#include <atomic>
#include <thread>
#include <vector>
#include <cstdint>
#include <cstddef>
class SymbolCache;
class LatencyTracker;
struct ParserStats;
class Visualizer {
public:
 Visualizer(const SymbolCache& cache,
 const LatencyTracker& lat,
 const ParserStats& pstats,
 size_t num_symbols);
 void start();
 void stop();
 void set_message_rate(double rate);
 void add_message_count(uint64_t n);
private:
 void render_loop();
 void render_frame();
 const SymbolCache& cache_;
 const LatencyTracker& lat_;
 const ParserStats& pstats_;
 size_t num_symbols_;
 std::atomic<bool> running_{false};
 std::atomic<uint64_t> total_msgs_{0};
 std::atomic<uint64_t> msg_rate_{0};
 std::thread thread_;
 std::vector<double> prev_prices_;
};
