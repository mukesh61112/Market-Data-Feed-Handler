#include "MarketDataSocket.h"

#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/select.h>
#include <netinet/tcp.h>
#include <errno.h>

// ---------------- Constructor ----------------
MarketDataSocket::MarketDataSocket()
    : sock_fd(-1), epoll_fd(-1), connected(false) {}

MarketDataSocket::~MarketDataSocket() {
    disconnect();
}

// ---------------- Connect with retry ----------------
bool MarketDataSocket::connect(const std::string& host, uint16_t port,
                               uint32_t timeout_ms) {
    int retry = 0;

    while (retry < 5) {
        sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_fd < 0) return false;

        // non-blocking
        fcntl(sock_fd, F_SETFL, O_NONBLOCK);

        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));

        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);

        if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
            close(sock_fd);
            return false;
        }

        int ret = ::connect(sock_fd, (sockaddr*)&addr, sizeof(addr));

        if (ret == 0) {
            connected = true;
        }
        else if (errno == EINPROGRESS) {
            if (!wait_for_connect(timeout_ms)) {
                close(sock_fd);
                retry++;
                usleep((1 << retry) * 100000);
                continue;
            }
            connected = true;
        }
        else {
            close(sock_fd);
            retry++;
            usleep((1 << retry) * 100000);
            continue;
        }

        // epoll setup
        epoll_fd = epoll_create1(0);
        if (epoll_fd < 0) {
            close(sock_fd);
            return false;
        }

        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = sock_fd;

        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock_fd, &ev);

        return true;
    }

    return false;
}

// ---------------- Wait for connect ----------------
bool MarketDataSocket::wait_for_connect(uint32_t timeout_ms) {
    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(sock_fd, &wfds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select(sock_fd + 1, NULL, &wfds, NULL, &tv);

    if (ret <= 0) return false;

    int err = 0;
    socklen_t len = sizeof(err);

    if (getsockopt(sock_fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0)
        return false;

    return err == 0;
}

// ---------------- Receive ----------------
ssize_t MarketDataSocket::receive(void* buffer, size_t max_len) {
    if (!connected) return -1;

    ssize_t total = 0;

    while (true) {
        ssize_t n = recv(sock_fd,
                         (char*)buffer + total,
                         max_len - total,
                         0);

        if (n > 0) {
            total += n;

            if ((size_t)total == max_len)
                break;
        }
        else if (n == 0) {
            connected = false;
            return -1;
        }
        else {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            else {
                connected = false;
                return -1;
            }
        }
    }

    return total;
}

// ---------------- Subscription ----------------
bool MarketDataSocket::send_subscription(const std::vector<uint16_t>& symbol_ids) {
    if (!connected) return false;

    uint16_t count = symbol_ids.size();
    uint16_t net_count = htons(count);

    size_t msg_size = 1 + 2 + count * 2;
    std::vector<char> buffer(msg_size);

    buffer[0] = 0xFF;

    std::memcpy(&buffer[1], &net_count, 2);
    std::memcpy(&buffer[3], symbol_ids.data(), count * 2);

    ssize_t sent = send(sock_fd, buffer.data(), buffer.size(), 0);

    return sent == (ssize_t)buffer.size();
}

// ---------------- Disconnect ----------------
void MarketDataSocket::disconnect() {
    if (sock_fd >= 0) {
        close(sock_fd);
        sock_fd = -1;
    }

    if (epoll_fd >= 0) {
        close(epoll_fd);
        epoll_fd = -1;
    }

    connected = false;
}

// ---------------- Status ----------------
bool MarketDataSocket::is_connected() const {
    return connected;
}

// ---------------- Socket Options ----------------
bool MarketDataSocket::set_tcp_nodelay(bool enable) {
    int flag = enable ? 1 : 0;
    return setsockopt(sock_fd, IPPROTO_TCP, TCP_NODELAY,
                      &flag, sizeof(flag)) == 0;
}

bool MarketDataSocket::set_recv_buffer_size(size_t bytes) {
    return setsockopt(sock_fd, SOL_SOCKET, SO_RCVBUF,
                      &bytes, sizeof(bytes)) == 0;
}

bool MarketDataSocket::set_socket_priority(int priority) {
    return setsockopt(sock_fd, SOL_SOCKET, SO_PRIORITY,
                      &priority, sizeof(priority)) == 0;
}
