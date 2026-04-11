#include <arpa/inet.h>
#include <unistd.h>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <cstring>

#pragma pack(push, 1)
struct Header {
    uint16_t msg_type;
    uint32_t seq_no;
    uint64_t timestamp;
    uint16_t symbol_id;
};

struct Trade {
    double price;
    uint32_t qty;
};
#pragma pack(pop)

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9000);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 10);

    std::cout << "Server started on port 9000\n";

    int client = accept(server_fd, NULL, NULL);
    std::cout << "Client connected\n";

    srand(time(NULL));
    uint32_t seq = 1;

    while (true) {
        Header h;
        h.msg_type = 0x01;
        h.seq_no = seq++;
        h.timestamp = time(NULL);
        h.symbol_id = rand() % 10;

        Trade t;
        t.price = 100 + rand() % 100;
        t.qty = rand() % 50;

        char buffer[64];
        memcpy(buffer, &h, sizeof(h));
        memcpy(buffer + sizeof(h), &t, sizeof(t));

        send(client, buffer, sizeof(h) + sizeof(t), 0);

        usleep(1000);
    }
}