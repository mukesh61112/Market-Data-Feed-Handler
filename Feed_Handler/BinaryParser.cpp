#include "BinaryParser.h"

#include <iostream>
#include <cstring>

// Message Types
#define MSG_TRADE     0x01
#define MSG_QUOTE     0x02
#define MSG_HEARTBEAT 0x03

// Header = 16 bytes
static const size_t HEADER_SIZE = 16;
static const size_t CHECKSUM_SIZE = 4;

BinaryParser::BinaryParser()
    : write_pos(0), read_pos(0), last_seq(0) {}

// ---------------- Feed Data ----------------
void BinaryParser::on_data(const char* data, size_t len) {
    // prevent overflow
    if (write_pos + len > BUFFER_SIZE) {
        // reset buffer (simple strategy)
        write_pos = 0;
        read_pos = 0;
    }

    std::memcpy(buffer + write_pos, data, len);
    write_pos += len;

    process_buffer();
}

// ---------------- Process Buffer ----------------
void BinaryParser::process_buffer() {
    while (true) {
        size_t available = write_pos - read_pos;

        if (available < HEADER_SIZE)
            return;

        const char* msg = buffer + read_pos;

        uint16_t msg_type;
        std::memcpy(&msg_type, msg, 2);

        size_t msg_size = get_message_size(msg_type);

        if (msg_size == 0) {
            // malformed → skip 1 byte
            read_pos += 1;
            continue;
        }

        if (available < msg_size)
            return; // wait more data

        if (parse_message(msg, msg_size)) {
            read_pos += msg_size;
        } else {
            // bad message → skip
            read_pos += 1;
        }

        // compact buffer if needed
        if (read_pos == write_pos) {
            read_pos = write_pos = 0;
        }
    }
}

// ---------------- Parse Message ----------------
bool BinaryParser::parse_message(const char* msg, size_t len) {
    // checksum check
    uint32_t recv_checksum;
    std::memcpy(&recv_checksum, msg + len - 4, 4);

    uint32_t calc_checksum = compute_checksum(msg, len - 4);

    if (recv_checksum != calc_checksum) {
        std::cout << "Checksum error\n";
        return false;
    }

    // header parsing
    uint16_t msg_type;
    uint32_t seq;
    uint64_t ts;
    uint16_t symbol;

    std::memcpy(&msg_type, msg, 2);
    std::memcpy(&seq, msg + 2, 4);
    std::memcpy(&ts, msg + 6, 8);
    std::memcpy(&symbol, msg + 14, 2);

    // sequence gap detection
    if (last_seq != 0 && seq != last_seq + 1) {
        std::cout << "Sequence gap detected: expected "
                  << (last_seq + 1) << " got " << seq << std::endl;
    }
    last_seq = seq;

    const char* payload = msg + HEADER_SIZE;

    if (msg_type == MSG_TRADE) {
        double price;
        uint32_t qty;

        std::memcpy(&price, payload, 8);
        std::memcpy(&qty, payload + 8, 4);

        // process trade (no copy)
        // std::cout << "Trade " << symbol << " " << price << " " << qty << "\n";
    }
    else if (msg_type == MSG_QUOTE) {
        double bid, ask;
        uint32_t bid_qty, ask_qty;

        std::memcpy(&bid, payload, 8);
        std::memcpy(&bid_qty, payload + 8, 4);
        std::memcpy(&ask, payload + 12, 8);
        std::memcpy(&ask_qty, payload + 20, 4);

        // process quote
    }
    else if (msg_type == MSG_HEARTBEAT) {
        // nothing
    }
    else {
        return false;
    }

    return true;
}

// ---------------- Message Size ----------------
size_t BinaryParser::get_message_size(uint16_t msg_type) {
    if (msg_type == MSG_TRADE)
        return HEADER_SIZE + 12 + CHECKSUM_SIZE;

    if (msg_type == MSG_QUOTE)
        return HEADER_SIZE + 24 + CHECKSUM_SIZE;

    if (msg_type == MSG_HEARTBEAT)
        return HEADER_SIZE + CHECKSUM_SIZE;

    return 0;
}

// ---------------- Checksum ----------------
uint32_t BinaryParser::compute_checksum(const char* data, size_t len) {
    uint32_t xor_sum = 0;

    for (size_t i = 0; i < len; i++) {
        xor_sum ^= (uint8_t)data[i];
    }

    return xor_sum;
}