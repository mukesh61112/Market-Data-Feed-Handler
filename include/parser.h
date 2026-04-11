#pragma once
#include <cstdint>
#include <functional>
//Parsed message (union variant)
struct ParsedMessage {
 uint16_t type;
 uint32_t seq_num;
 uint64_t timestamp;
 uint16_t symbol_id;
 union {
 struct { double price; uint32_t quantity; } trade;
 struct { double bid_price; uint32_t bid_qty;
 double ask_price; uint32_t ask_qty; } quote;
 } u;
};
using ParseCallback = std::function<void(const ParsedMessage&)>;
struct ParserStats {
 uint64_t messages_parsed = 0;
 uint64_t checksum_errors = 0;
 uint64_t seq_gaps = 0;
 uint64_t malformed = 0;
 uint64_t buffer_overflows = 0;
};
class MessageParser {
public:
 // Feed raw bytes from socket; cb is called for each complete message
 void feed(const uint8_t* data, size_t len, ParseCallback cb);
 const ParserStats& get_stats() const;
 void reset_stats();
private:
 static constexpr size_t BUF_SIZE = 256 * 1024; // 256 KB reassembly buffer
 uint8_t buf_[BUF_SIZE] = {};
 size_t fill_ = 0;
 uint32_t last_seq_ = 0;
 ParserStats stats_;
};
