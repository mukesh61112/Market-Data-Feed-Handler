#ifndef MARKET_DATA_SOCKET_H
#define MARKET_DATA_SOCKET_H

#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <sys/types.h>

class MarketDataSocket {
public:
    MarketDataSocket();
    ~MarketDataSocket();

    bool connect(const std::string& host, uint16_t port,
                 uint32_t timeout_ms = 5000);

    ssize_t receive(void* buffer, size_t max_len);

    bool send_subscription(const std::vector<uint16_t>& symbol_ids);

    bool is_connected() const;
    void disconnect();

    bool set_tcp_nodelay(bool enable);
    bool set_recv_buffer_size(size_t bytes);
    bool set_socket_priority(int priority);

private:
    int sock_fd;
    int epoll_fd;
    bool connected;

    bool wait_for_connect(uint32_t timeout_ms);
};

#endif