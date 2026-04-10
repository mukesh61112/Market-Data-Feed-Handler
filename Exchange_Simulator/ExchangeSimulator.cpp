#include "ExchangeSimulator.h"

#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <cmath>
#include <cstdlib>
#include <ctime>

// ------------------ Constructor ------------------
ExchangeSimulator::ExchangeSimulator(uint16_t p, size_t n)
    : port(p), num_symbols(n), tick_rate(10000), fault_injection(false) {

    server_fd = -1;
    epoll_fd = -1;

    std::srand(std::time(NULL));

    symbols.resize(num_symbols);

    for (size_t i = 0; i < num_symbols; i++) {
        symbols[i].price = 100 + (std::rand() % 5000);

        // neutral market default
        symbols[i].mu = 0.0;

        // sigma ∈ [0.01, 0.06]
        symbols[i].sigma = 0.01 + ((double)std::rand() / RAND_MAX) * 0.05;
    }
}

// ------------------ Start ------------------
void ExchangeSimulator::start() {
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    //addr.sin_addr.s_addr = "127.0.0.1"; // INADDR_ANY;
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    bind(server_fd, (sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 128);

    // non-blocking
    fcntl(server_fd, F_SETFL, O_NONBLOCK);

    epoll_fd = epoll_create1(0);

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;

    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev);

    std::cout << "Server started on port " << port << std::endl;
}

// ------------------ Run Loop ------------------
void ExchangeSimulator::run() {
    const int MAX_EVENTS = 64;
    struct epoll_event events[MAX_EVENTS];

    double dt = 0.001; // 1 ms

    while (true) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 1);

        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;

            if (fd == server_fd) {
                handle_new_connection();
            } else {
                // detect disconnect
                char buf;
                int ret = recv(fd, &buf, 1, MSG_DONTWAIT);

                if (ret <= 0) {
                    handle_client_disconnect(fd);
                }
            }
        }

        // generate ticks
        for (uint16_t i = 0; i < num_symbols; i++) {
            generate_tick(i);
        }
    }
}

// ------------------ New Connection ------------------
void ExchangeSimulator::handle_new_connection() {
    int client_fd = accept(server_fd, NULL, NULL);

    if (client_fd < 0) return;

    fcntl(client_fd, F_SETFL, O_NONBLOCK);

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = client_fd;

    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);

    clients.push_back(client_fd);

    std::cout << "Client connected: " << client_fd << std::endl;
}

// ------------------ Disconnect ------------------
void ExchangeSimulator::handle_client_disconnect(int client_fd) {
    close(client_fd);

    for (size_t i = 0; i < clients.size(); i++) {
        if (clients[i] == client_fd) {
            clients.erase(clients.begin() + i);
            break;
        }
    }

    std::cout << "Client disconnected: " << client_fd << std::endl;
}

// ------------------ Box-Muller ------------------
double ExchangeSimulator::generate_normal() {
    double u1 = (double)rand() / RAND_MAX;
    double u2 = (double)rand() / RAND_MAX;

    return std::sqrt(-2.0 * std::log(u1)) * std::cos(2 * M_PI * u2);
}

// ------------------ Tick Generation (GBM) ------------------
void ExchangeSimulator::generate_tick(uint16_t symbol_id) {
    Symbol &s = symbols[symbol_id];

    double dt = 0.001;

    double dW = std::sqrt(dt) * generate_normal();

    // GBM: dS = μSdt + σS dW
    double dS = s.mu * s.price * dt + s.sigma * s.price * dW;

    s.price += dS;

    if (s.price < 1.0) s.price = 1.0;

    // spread 0.05% - 0.2%
    double spread_pct = 0.0005 + ((double)rand() / RAND_MAX) * 0.0015;
    double spread = s.price * spread_pct;

    double bid = s.price - spread / 2;
    double ask = s.price + spread / 2;

    struct Tick {
        uint16_t symbol_id;
        double bid;
        double ask;
        double last_price;
    } tick;

    tick.symbol_id = symbol_id;
    tick.bid = bid;
    tick.ask = ask;
    tick.last_price = s.price;

    // fault injection
    if (fault_injection && (rand() % 50 == 0)) {
        return;
    }

    broadcast_message(&tick, sizeof(tick));
}

// ------------------ Broadcast ------------------
void ExchangeSimulator::broadcast_message(const void* data, size_t len) {
    for (size_t i = 0; i < clients.size(); i++) {
        int fd = clients[i];

        int ret = send(fd, data, len, MSG_DONTWAIT);

        // flow control: drop slow clients
        if (ret < 0) {
            handle_client_disconnect(fd);
            clients.erase(clients.begin() + i);
            i--;
        }
    }
}

// ------------------ Config ------------------
void ExchangeSimulator::set_tick_rate(uint32_t t) {
    tick_rate = t;
}

void ExchangeSimulator::enable_fault_injection(bool enable) {
    fault_injection = enable;
}
