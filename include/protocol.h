#pragma once
#include <cstdint>
#include <cstring>
//  Message Types
constexpr uint16_t MSG_TRADE = 0x01;
constexpr uint16_t MSG_QUOTE = 0x02;
constexpr uint16_t MSG_HEARTBEAT = 0x03;
constexpr uint16_t MSG_SUBSCRIBE = 0xFF;
//  Header (16 bytes) 
#pragma pack(push, 1)
struct MessageHeader {
 uint16_t msg_type; // 2 bytes
 uint32_t seq_num; // 4 bytes
 uint64_t timestamp_ns; // 8 bytes (nanoseconds since epoch)
 uint16_t symbol_id; // 2 bytes
};
//  Trade Payload (12 bytes) 
struct TradePayload {
 double price; // 8 bytes
 uint32_t quantity; // 4 bytes
};
//  Quote Payload (24 bytes) 
struct QuotePayload {
 double bid_price; // 8 bytes
 uint32_t bid_qty; // 4 bytes
 double ask_price; // 8 bytes
 uint32_t ask_qty; // 4 bytes
};
//  Full Messages 
struct TradeMessage {
 MessageHeader header;
 TradePayload payload;
 uint32_t checksum; // XOR of all previous bytes
};
struct QuoteMessage {
 MessageHeader header;
 QuotePayload payload;
 uint32_t checksum;
};
struct HeartbeatMessage {
 MessageHeader header;
 uint32_t checksum;
};
//  Subscription Request 
struct SubscribeHeader {
 uint8_t cmd; // 0xFF
 uint16_t count; // number of symbol IDs following
 // followed by count * uint16_t symbol_ids
};
#pragma pack(pop)
//  Sizes 
constexpr size_t TRADE_MSG_SIZE = sizeof(TradeMessage);
constexpr size_t QUOTE_MSG_SIZE = sizeof(QuoteMessage);
constexpr size_t HEARTBEAT_MSG_SIZE = sizeof(HeartbeatMessage);
constexpr size_t HEADER_SIZE = sizeof(MessageHeader);
//  Checksum helper 
inline uint32_t compute_checksum(const void* data, size_t len) {
 const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
 uint32_t xor_val = 0;
 for (size_t i = 0; i < len; ++i)
 xor_val ^= static_cast<uint32_t>(p[i]);
 return xor_val;
}
//  Timestamp helper 
#include <chrono>
inline uint64_t now_ns() {
 return static_cast<uint64_t>(
 std::chrono::duration_cast<std::chrono::nanoseconds>(
 std::chrono::system_clock::now().time_since_epoch()).count());
}
