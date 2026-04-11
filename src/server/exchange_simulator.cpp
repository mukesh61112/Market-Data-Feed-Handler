// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// exchange_simulator.cpp
//
// TCP Server (epoll-based) that generates realistic NSE market data ticks
// using Geometric Brownian Motion and broadcasts them to all connected clients.
//
// Build: see CMakeLists.txt
// Run : ./exchange_simulator [port] [num_symbols] [tick_rate]
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
#include "../include/protocol.h"
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <random>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <cassert>
// ■■■ Symbol State ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
struct SymbolState {
 double price; // current mid price
 double mu; // drift
 double sigma; // volatility
 double spread_pct; // bid-ask spread as fraction of price
};
// ■■■ Box-Muller normal random ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
static double box_muller(std::mt19937_64& rng) {
 static thread_local double spare;
 static thread_local bool has_spare = false;
 if (has_spare) { has_spare = false; return spare; }
 std::uniform_real_distribution<double> u(1e-10, 1.0);
 double u1 = u(rng), u2 = u(rng);
 double mag = std::sqrt(-2.0 * std::log(u1));
 spare = mag * std::cos(2.0 * M_PI * u2);
 has_spare = true;
 return mag * std::sin(2.0 * M_PI * u2);
}
// ■■■ GBM step ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// S(t+dt) = S(t) * exp((mu - 0.5*sigma^2)*dt + sigma*sqrt(dt)*Z)
static double gbm_step(double S, double mu, double sigma,
 double dt, std::mt19937_64& rng) {
 double z = box_muller(rng);
 return S * std::exp((mu - 0.5 * sigma * sigma) * dt + sigma * std::sqrt(dt) * z);
}
// ■■■ Make non-blocking ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
static void set_nonblocking(int fd) {
 int flags = fcntl(fd, F_GETFL, 0);
 fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
// ■■■ ExchangeSimulator ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
class ExchangeSimulator {
public:
 explicit ExchangeSimulator(uint16_t port, size_t num_symbols = 100,
 uint32_t tick_rate = 100'000)
 : port_(port), num_symbols_(num_symbols), tick_rate_(tick_rate)
 {
 rng_.seed(std::random_device{}());
 init_symbols();
 }
 void start() {
 // Create listening socket
 server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
 if (server_fd_ < 0) { perror("socket"); exit(1); }
 int opt = 1;
 setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
 setsockopt(server_fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
 set_nonblocking(server_fd_);
 sockaddr_in addr{};
 addr.sin_family = AF_INET;
 addr.sin_addr.s_addr = INADDR_ANY;
 addr.sin_port = htons(port_);
 if (bind(server_fd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
 perror("bind"); exit(1);
 }
 listen(server_fd_, 128);
 // epoll
 epoll_fd_ = epoll_create1(0);
 epoll_event ev{};
 ev.events = EPOLLIN;
 ev.data.fd = server_fd_;
 epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, server_fd_, &ev);
 printf("[Server] Listening on port %d symbols=%zu tick_rate=%u/s\n",
 port_, num_symbols_, tick_rate_.load());
 running_.store(true);
 }
 void run() {
 // Launch tick generation thread
 auto tick_thread = std::thread([this]{ tick_loop(); });
 // Network event loop (main thread)
 constexpr int MAX_EVENTS = 64;
 epoll_event events[MAX_EVENTS];
 while (running_.load()) {
 int n = epoll_wait(epoll_fd_, events, MAX_EVENTS, 1 /*ms*/);
 for (int i = 0; i < n; ++i) {
 if (events[i].data.fd == server_fd_) {
 handle_new_connection();
 } else if (events[i].events & (EPOLLHUP | EPOLLERR)) {
 handle_client_disconnect(events[i].data.fd);
 }
 // We don't read from clients here (fire-and-forget broadcast)
 }
 }
 tick_thread.join();
 close(epoll_fd_);
 close(server_fd_);
 }
 void set_tick_rate(uint32_t tps) { tick_rate_ = tps; }
 void enable_fault_injection(bool e) { fault_inject_ = e; }
 void stop() { running_.store(false); }
private:
 // ■■ Symbol initialisation ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
 void init_symbols() {
 std::uniform_real_distribution<double> price_dist(100.0, 5000.0);
 std::uniform_real_distribution<double> sigma_dist(0.01, 0.06);
 std::uniform_real_distribution<double> spread_dist(0.0005, 0.002);
 symbols_.resize(num_symbols_);
 for (size_t i = 0; i < num_symbols_; ++i) {
 symbols_[i].price = price_dist(rng_);
 symbols_[i].mu = 0.0; // neutral drift
 symbols_[i].sigma = sigma_dist(rng_);
 symbols_[i].spread_pct = spread_dist(rng_);
 }
 }
 // ■■ Accept new TCP connection ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
 void handle_new_connection() {
 sockaddr_in cli{};
 socklen_t len = sizeof(cli);
 int cfd = accept(server_fd_, (sockaddr*)&cli, &len);
 if (cfd < 0) return;
 set_nonblocking(cfd);
 // Low-latency socket options
 int one = 1;
 setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
 // Large send buffer
 int bufsize = 4 * 1024 * 1024;
 setsockopt(cfd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize));
 epoll_event ev{};
 ev.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLET;
 ev.data.fd = cfd;
 epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, cfd, &ev);
 {
 std::lock_guard<std::mutex> lk(clients_mtx_);
 clients_.push_back(cfd);
 }
 printf("[Server] Client connected fd=%d total=%zu\n",
 cfd, clients_.size());
 }
 // ■■ Remove disconnected client ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
 void handle_client_disconnect(int fd) {
 epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
 close(fd);
 std::lock_guard<std::mutex> lk(clients_mtx_);
 clients_.erase(std::remove(clients_.begin(), clients_.end(), fd),
 clients_.end());
 printf("[Server] Client disconnected fd=%d remaining=%zu\n",
 fd, clients_.size());
 }
 // ■■ Broadcast raw bytes to all clients ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
 void broadcast_message(const void* data, size_t len) {
 std::lock_guard<std::mutex> lk(clients_mtx_);
 std::vector<int> bad;
 for (int fd : clients_) {
 ssize_t sent = 0;
 while (sent < (ssize_t)len) {
 ssize_t r = send(fd, (const char*)data + sent, len - sent,
 MSG_NOSIGNAL);
 if (r < 0) {
 if (errno == EAGAIN || errno == EWOULDBLOCK) {
 // Slow consumer: drop this client
 bad.push_back(fd);
 break;
 }
 bad.push_back(fd);
 break;
 }
 sent += r;
 }
 }
 for (int fd : bad) {
 epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
 close(fd);
 clients_.erase(std::remove(clients_.begin(), clients_.end(), fd),
 clients_.end());
 }
 }
 // ■■ Generate one tick for symbol_id ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
 void generate_tick(uint16_t symbol_id) {
 auto& sym = symbols_[symbol_id];
 constexpr double dt = 0.001; // 1 ms time step
 sym.price = gbm_step(sym.price, sym.mu, sym.sigma, dt, rng_);
 sym.price = std::max(sym.price, 1.0); // floor at ■1
 double half_spread = sym.price * sym.spread_pct * 0.5;
 double bid = sym.price - half_spread;
 double ask = sym.price + half_spread;
 // 70% Quote, 30% Trade
 std::uniform_real_distribution<double> coin(0.0, 1.0);
 bool is_quote = (coin(rng_) < 0.70);
 uint32_t seq = ++seq_num_;
 uint64_t ts = now_ns();
 if (is_quote) {
 QuoteMessage msg{};
 msg.header.msg_type = MSG_QUOTE;
 msg.header.seq_num = seq;
 msg.header.timestamp_ns = ts;
 msg.header.symbol_id = symbol_id;
 msg.payload.bid_price = bid;
 msg.payload.bid_qty = std::uniform_int_distribution<uint32_t>(100, 5000)(rng_);
 msg.payload.ask_price = ask;
 msg.payload.ask_qty = std::uniform_int_distribution<uint32_t>(100, 5000)(rng_);
 msg.checksum = compute_checksum(&msg, sizeof(msg) - 4);
 broadcast_message(&msg, sizeof(msg));
 } else {
 TradeMessage msg{};
 msg.header.msg_type = MSG_TRADE;
 msg.header.seq_num = seq;
 msg.header.timestamp_ns = ts;
 msg.header.symbol_id = symbol_id;
 msg.payload.price = sym.price;
 msg.payload.quantity = std::uniform_int_distribution<uint32_t>(10, 1000)(rng_);
 msg.checksum = compute_checksum(&msg, sizeof(msg) - 4);
 broadcast_message(&msg, sizeof(msg));
 }
 }
 // ■■ Tick generation loop (runs in separate thread) ■■■■■■■■■■■■■■■■■■■■■■■■
 void tick_loop() {
 using clock = std::chrono::steady_clock;
 using ns = std::chrono::nanoseconds;
 uint32_t sym_idx = 0;
 // Time between ticks = 1s / tick_rate
 while (running_.load()) {
 auto t0 = clock::now();
 uint32_t rate = tick_rate_.load();
 uint64_t ns_per_tick = 1'000'000'000ULL / rate;
 generate_tick(sym_idx % num_symbols_);
 ++sym_idx;
 // Heartbeat every 1000 ticks
 if (sym_idx % 1000 == 0) {
 HeartbeatMessage hb{};
 hb.header.msg_type = MSG_HEARTBEAT;
 hb.header.seq_num = ++seq_num_;
 hb.header.timestamp_ns = now_ns();
 hb.header.symbol_id = 0;
 hb.checksum = compute_checksum(&hb, sizeof(hb) - 4);
 broadcast_message(&hb, sizeof(hb));
 }
 // Spin-sleep to maintain rate
 auto deadline = t0 + ns(ns_per_tick);
 while (clock::now() < deadline) {
 std::this_thread::yield();
 }
 }
 }
 // ■■ Data members ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
 uint16_t port_;
 size_t num_symbols_;
 std::atomic<uint32_t> tick_rate_;
 bool fault_inject_ = false;
 int server_fd_ = -1;
 int epoll_fd_ = -1;
 std::atomic<bool> running_{false};
 std::atomic<uint32_t> seq_num_{0};
 std::vector<SymbolState> symbols_;
 std::mt19937_64 rng_;
 std::vector<int> clients_;
 std::mutex clients_mtx_;
};
// ■■■ main ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
int main(int argc, char* argv[]) {
 uint16_t port = (argc > 1) ? (uint16_t)atoi(argv[1]) : 9876;
 size_t num_symbols = (argc > 2) ? (size_t)atoi(argv[2]) : 100;
 uint32_t tick_rate = (argc > 3) ? (uint32_t)atoi(argv[3]) : 100'000;
 ExchangeSimulator sim(port, num_symbols, tick_rate);
 sim.start();
 sim.run();
 return 0;
}
