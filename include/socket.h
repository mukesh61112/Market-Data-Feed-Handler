#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <sys/epoll.h>
class MarketDataSocket {
public:
 MarketDataSocket() = default;
 ~MarketDataSocket() { disconnect(); }
 bool connect(const std::string& host, uint16_t port,
 uint32_t timeout_ms = 5000);
 ssize_t receive(void* buffer, size_t max_len);
 bool send_subscription(const std::vector<uint16_t>& symbol_ids);
 bool is_connected() const;
 void disconnect();
 bool set_tcp_nodelay(bool enable);
 bool set_recv_buffer_size(size_t bytes);
 bool set_socket_priority(int priority);
 bool wait_readable(int timeout_ms = 5);
 int fd() const { return fd_; }
 int epoll_fd() const { return epoll_fd_; }
private:
 void setup_epoll();
 int fd_ = -1;
 int epoll_fd_ = -1;
 std::atomic<bool> connected_{false};
 std::string host_;
 uint16_t port_ = 0;
};
