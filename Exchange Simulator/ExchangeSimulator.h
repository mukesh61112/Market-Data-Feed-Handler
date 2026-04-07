#ifndef EXCHANGE_SIMULATOR_H
#define EXCHANGE_SIMULATOR_H

#include <vector>
#include <cstdint>
#include <cstddef>

class ExchangeSimulator {
public:
    ExchangeSimulator(uint16_t port, size_t num_symbols = 100);

    void start();
    void run();

    void set_tick_rate(uint32_t ticks_per_second);
    void enable_fault_injection(bool enable);

private:
    int server_fd;
    int epoll_fd;
    uint16_t port;
    size_t num_symbols;

    struct Symbol {
        double price;
        double mu;
        double sigma;
    };

    std::vector<Symbol> symbols;
    std::vector<int> clients;

    uint32_t tick_rate;
    bool fault_injection;

private:
    void handle_new_connection();
    void handle_client_disconnect(int client_fd);

    void generate_tick(uint16_t symbol_id);
    double generate_normal(); // Box-Muller

    void broadcast_message(const void* data, size_t len);
};

#endif
