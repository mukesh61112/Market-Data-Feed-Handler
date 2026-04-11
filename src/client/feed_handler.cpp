// ─────────────────────────────────────────────────────────────────────────────
// feed_handler.cpp  –  Main market data client
//
// Connects to exchange simulator, receives ticks, parses them,
// updates lock-free symbol cache, tracks latency, drives visualizer.
//
// Run: ./feed_handler [host] [port] [num_symbols]
// ─────────────────────────────────────────────────────────────────────────────
#include "../include/protocol.h"
#include "../include/socket.h"
#include "../include/parser.h"
#include "../include/cache.h"
#include "../include/latency_tracker.h"
#include "../include/visualizer.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <numeric>
#include <string>
#include <termios.h>
#include <unistd.h>

// ─── Global shutdown flag ─────────────────────────────────────────────────────
static std::atomic<bool> g_running{true};
static void sig_handler(int) { g_running.store(false); }

// ─── Keyboard thread – watches for 'q' / 'r' ─────────────────────────────────
static void keyboard_loop(LatencyTracker& lat) {
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    while (g_running.load()) {
        fd_set fds; FD_ZERO(&fds); FD_SET(STDIN_FILENO, &fds);
        timeval tv{0, 100000}; // 100 ms
        if (select(1, &fds, nullptr, nullptr, &tv) > 0) {
            char c = 0;
            (void)read(STDIN_FILENO, &c, 1);
            if (c == 'q') g_running.store(false);
            if (c == 'r') lat.reset();
        }
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
}

// ─── Rate calculator ──────────────────────────────────────────────────────────
struct RateCalc {
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    uint64_t count = 0;
    double   rate  = 0.0;

    void tick(uint64_t n = 1) {
        count += n;
        auto now  = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - t0).count();
        if (dt >= 1.0) {
            rate  = count / dt;
            count = 0;
            t0    = now;
        }
    }
};

// ─── FeedHandler ─────────────────────────────────────────────────────────────
class FeedHandler {
public:
    FeedHandler(const std::string& host, uint16_t port, size_t num_symbols)
        : host_(host), port_(port), num_symbols_(num_symbols),
          viz_(cache_, lat_, parser_.get_stats(), num_symbols)
    {}

    void run() {
        signal(SIGINT,  sig_handler);
        signal(SIGTERM, sig_handler);

        // Subscribe to all symbols
        std::vector<uint16_t> sym_ids(num_symbols_);
        std::iota(sym_ids.begin(), sym_ids.end(), 0);

        viz_.start();
        auto kb_thread = std::thread([this]{ keyboard_loop(lat_); });

        uint32_t reconnect_delay_ms = 500;

        while (g_running.load()) {
            // ── Connect ──────────────────────────────────────────────────────
            printf("[FeedHandler] Connecting to %s:%d...\n",
                   host_.c_str(), port_);
            if (!sock_.connect(host_, port_)) {
                printf("[FeedHandler] Connection failed. Retry in %u ms\n",
                       reconnect_delay_ms);
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(reconnect_delay_ms));
                reconnect_delay_ms = std::min(reconnect_delay_ms * 2, (uint32_t)8000);
                continue;
            }
            reconnect_delay_ms = 500; // reset backoff

            sock_.send_subscription(sym_ids);
            parser_.reset_stats();

            // ── Receive loop ─────────────────────────────────────────────────
            static constexpr size_t RBUF_SIZE = 128 * 1024;
            uint8_t rbuf[RBUF_SIZE];
            RateCalc rate_calc;

            while (g_running.load() && sock_.is_connected()) {
                if (!sock_.wait_readable(5)) continue; // 5 ms timeout

                while (true) {
                    ssize_t n = sock_.receive(rbuf, RBUF_SIZE);
                    if (n == 0) break;               // EAGAIN
                    if (n < 0) goto reconnect;       // disconnected

                    // Parse and process
                    parser_.feed(rbuf, (size_t)n,
                        [&](const ParsedMessage& pm) {
                            uint64_t latency = now_ns() - pm.timestamp;
                            lat_.record(latency);
                            apply_to_cache(pm);
                            rate_calc.tick();
                            viz_.add_message_count(1);
                        });

                    viz_.set_message_rate(rate_calc.rate);
                }
                continue;
                reconnect:
                    printf("[FeedHandler] Disconnected – reconnecting...\n");
                    sock_.disconnect();
                    break;
            }
        }

        viz_.stop();
        sock_.disconnect();
        kb_thread.join();
    }

private:
    void apply_to_cache(const ParsedMessage& pm) {
        if (pm.symbol_id >= num_symbols_) return;
        if (pm.type == MSG_QUOTE) {
            cache_.updateBid(pm.symbol_id, pm.u.quote.bid_price,
                             pm.u.quote.bid_qty, pm.timestamp);
            cache_.updateAsk(pm.symbol_id, pm.u.quote.ask_price,
                             pm.u.quote.ask_qty, pm.timestamp);
        } else if (pm.type == MSG_TRADE) {
            cache_.updateTrade(pm.symbol_id, pm.u.trade.price,
                               pm.u.trade.quantity, pm.timestamp);
        }
    }

    std::string    host_;
    uint16_t       port_;
    size_t         num_symbols_;

    MarketDataSocket sock_;
    MessageParser    parser_;
    SymbolCache      cache_;
    LatencyTracker   lat_;
    Visualizer       viz_;
};

// ─── main ─────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    std::string host        = (argc > 1) ? argv[1]         : "127.0.0.1";
    uint16_t    port        = (argc > 2) ? (uint16_t)atoi(argv[2]) : 9876;
    size_t      num_symbols = (argc > 3) ? (size_t)atoi(argv[3])   : 100;

    FeedHandler handler(host, port, num_symbols);
    handler.run();
    return 0;
}
