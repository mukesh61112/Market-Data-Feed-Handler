#include "MarketDataSocket.h"
#include <iostream>
#include <vector>
#include "BinaryParser.h"

int main() {
    MarketDataSocket client;
    BinaryParser parser;

    if (!client.connect("127.0.0.1", 9000)) {
        std::cout << "Connection failed\n";
        return -1;
    }

    client.set_tcp_nodelay(true);
    client.set_recv_buffer_size(4 * 1024 * 1024);

    std::vector<uint16_t> symbols;
    for (int i = 0; i < 500; i++) {
        symbols.push_back(i);
    }

    client.send_subscription(symbols);

    char buffer[4096];

    while (true) {
        ssize_t n = client.receive(buffer, sizeof(buffer));

        if (n <= 0) {
            std::cout << "Disconnected. Reconnecting...\n";
            client.disconnect();

            if (!client.connect("127.0.0.1", 9000)) {
                std::cout << "Reconnect failed\n";
                break;
            }

            client.send_subscription(symbols);
            continue;
        }
        parser.on_data(buffer, n);
        std::cout << "Received bytes: " << n << std::endl;
    }

    return 0;
}