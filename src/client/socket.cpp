// 
// socket.cpp – Non-blocking TCP client socket for the market data feed
//
// Features:
// • epoll edge-triggered non-blocking I/O
// • Connection retry with exponential backoff
// • TCP_NODELAY + 4 MB receive buffer
// • Partial-read handling
// • Silent-drop detection via heartbeat timeout
// 
#include "../include/socket.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <thread>
//  MarketDataSocket implementation 
bool MarketDataSocket::connect(const std::string& host, uint16_t port,
 uint32_t timeout_ms) {
 host_ = host;
 port_ = port;
 uint32_t backoff_ms = 100;
 for (int attempt = 0; attempt < 10; ++attempt) {
 fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
 if (fd_ < 0) { perror("socket"); return false; }
 // Non-blocking
 int flags = fcntl(fd_, F_GETFL, 0);
 fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
 // TCP options
 set_tcp_nodelay(true);
 set_recv_buffer_size(4 * 1024 * 1024);
 sockaddr_in addr{};
 addr.sin_family = AF_INET;
 addr.sin_port = htons(port);
 inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
 int r = ::connect(fd_, (sockaddr*)&addr, sizeof(addr));
 if (r == 0 || errno == EINPROGRESS) {
 // Wait for writable
 fd_set wfds; FD_ZERO(&wfds); FD_SET(fd_, &wfds);
 timeval tv{ (long)(timeout_ms / 1000),
 (long)((timeout_ms % 1000) * 1000) };
 int sel = select(fd_ + 1, nullptr, &wfds, nullptr, &tv);
 if (sel > 0) {
 int err = 0; socklen_t len = sizeof(err);
 getsockopt(fd_, SOL_SOCKET, SO_ERROR, &err, &len);
 if (err == 0) {
 connected_.store(true);
 setup_epoll();
 printf("[Client] Connected to %s:%d\n", host.c_str(), port);
 return true;
 }
 }
 }
 ::close(fd_);
 fd_ = -1;
 printf("[Client] Connect attempt %d failed, retry in %u ms\n",
 attempt + 1, backoff_ms);
 std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
 backoff_ms = std::min(backoff_ms * 2, (uint32_t)8000); // cap 8 s
 }
 return false;
}
// Non-blocking receive; returns bytes read, 0=would-block, <0=error/closed
ssize_t MarketDataSocket::receive(void* buffer, size_t max_len) {
 ssize_t n = recv(fd_, buffer, max_len, 0);
 if (n > 0) return n;
 if (n == 0) { connected_.store(false); return -1; } // clean close
 if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
 connected_.store(false);
 return -1;
}
bool MarketDataSocket::send_subscription(const std::vector<uint16_t>& ids) {
 size_t pkt_len = 3 + ids.size() * 2; // 1 cmd + 2 count + N*2
 std::vector<uint8_t> buf(pkt_len);
 buf[0] = 0xFF; // Subscribe cmd
 uint16_t cnt = static_cast<uint16_t>(ids.size());
 memcpy(&buf[1], &cnt, 2);
 for (size_t i = 0; i < ids.size(); ++i)
 memcpy(&buf[3 + i * 2], &ids[i], 2);
 ssize_t sent = 0;
 while (sent < (ssize_t)pkt_len) {
 ssize_t r = send(fd_, buf.data() + sent, pkt_len - sent, MSG_NOSIGNAL);
 if (r < 0) return false;
 sent += r;
 }
 return true;
}
void MarketDataSocket::disconnect() {
 connected_.store(false);
 if (epoll_fd_ >= 0) { close(epoll_fd_); epoll_fd_ = -1; }
 if (fd_ >= 0) { close(fd_); fd_ = -1; }
}
bool MarketDataSocket::is_connected() const { return connected_.load(); }
bool MarketDataSocket::set_tcp_nodelay(bool enable) {
 int v = enable ? 1 : 0;
 return setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &v, sizeof(v)) == 0;
}
bool MarketDataSocket::set_recv_buffer_size(size_t bytes) {
 int v = static_cast<int>(bytes);
 return setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &v, sizeof(v)) == 0;
}
bool MarketDataSocket::set_socket_priority(int priority) {
 return setsockopt(fd_, SOL_SOCKET, SO_PRIORITY, &priority, sizeof(priority)) == 0;
}
// Wait for EPOLLIN event (edge-triggered). Returns true if data available.
bool MarketDataSocket::wait_readable(int timeout_ms) {
 epoll_event ev;
 int n = epoll_wait(epoll_fd_, &ev, 1, timeout_ms);
 return (n > 0) && (ev.events & EPOLLIN);
}
void MarketDataSocket::setup_epoll() {
 epoll_fd_ = epoll_create1(0);
 epoll_event ev{};
 ev.events = EPOLLIN | EPOLLET | EPOLLHUP | EPOLLERR;
 ev.data.fd = fd_;
 epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd_, &ev);
}
