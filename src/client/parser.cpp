// 
// parser.cpp – Zero-copy binary message parser
//
// Handles:
// • TCP stream fragmentation (partial messages buffered)
// • Sequence gap detection
// • Checksum validation
// • No dynamic memory allocation in hot path
// 
#include "../include/parser.h"
#include "../include/protocol.h"
#include <cstring>
#include <cstdio>
//  MessageParser implementation 
void MessageParser::feed(const uint8_t* data, size_t len,
 ParseCallback cb) {
 // Append incoming bytes to ring buffer
 size_t remaining = len;
 while (remaining > 0) {
 size_t space = BUF_SIZE - fill_;
 if (space == 0) {
 // Buffer full – discard oldest chunk equal to one max message
 constexpr size_t DISCARD = QUOTE_MSG_SIZE * 2;
 memmove(buf_, buf_ + DISCARD, fill_ - DISCARD);
 fill_ -= DISCARD;
 ++stats_.buffer_overflows;
 space = BUF_SIZE - fill_;
 }
 size_t copy = std::min(remaining, space);
 memcpy(buf_ + fill_, data, copy);
 fill_ += copy;
 data += copy;
 remaining -= copy;
 }
 // Parse as many complete messages as possible
 while (fill_ >= HEADER_SIZE) {
 const MessageHeader* hdr =
 reinterpret_cast<const MessageHeader*>(buf_);
 // Determine expected total message size
 size_t msg_size = 0;
 switch (hdr->msg_type) {
 case MSG_TRADE: msg_size = TRADE_MSG_SIZE; break;
 case MSG_QUOTE: msg_size = QUOTE_MSG_SIZE; break;
 case MSG_HEARTBEAT: msg_size = HEARTBEAT_MSG_SIZE; break;
 default:
 // Unknown type: resync by skipping 1 byte
 memmove(buf_, buf_ + 1, fill_ - 1);
 --fill_;
 ++stats_.malformed;
 continue;
 }
 if (fill_ < msg_size) break; // Wait for more bytes
 // Validate checksum (last 4 bytes = XOR of all prior bytes)
 uint32_t expected = compute_checksum(buf_, msg_size - 4);
 uint32_t actual;
 memcpy(&actual, buf_ + msg_size - 4, 4);
 if (expected != actual) {
 // Bad checksum: skip one byte and try re-syncing
 memmove(buf_, buf_ + 1, fill_ - 1);
 --fill_;
 ++stats_.checksum_errors;
 continue;
 }
 // Sequence gap detection
 uint32_t seq = hdr->seq_num;
 if (last_seq_ != 0 && seq != last_seq_ + 1) {
 uint32_t gap = seq - last_seq_ - 1;
 stats_.seq_gaps += gap;
 // Note: for TCP in-order delivery, gaps indicate server drops
 }
 last_seq_ = seq;
 // Dispatch
 ParsedMessage pm;
 pm.type = hdr->msg_type;
 pm.seq_num = hdr->seq_num;
 pm.timestamp = hdr->timestamp_ns;
 pm.symbol_id = hdr->symbol_id;
 if (hdr->msg_type == MSG_TRADE) {
 const TradeMessage* tm = reinterpret_cast<const TradeMessage*>(buf_);
 pm.u.trade.price = tm->payload.price;
 pm.u.trade.quantity = tm->payload.quantity;
 } else if (hdr->msg_type == MSG_QUOTE) {
 const QuoteMessage* qm = reinterpret_cast<const QuoteMessage*>(buf_);
 pm.u.quote.bid_price = qm->payload.bid_price;
 pm.u.quote.bid_qty = qm->payload.bid_qty;
 pm.u.quote.ask_price = qm->payload.ask_price;
 pm.u.quote.ask_qty = qm->payload.ask_qty;
 }
 ++stats_.messages_parsed;
 cb(pm);
 // Consume bytes
 memmove(buf_, buf_ + msg_size, fill_ - msg_size);
 fill_ -= msg_size;
 }
}
const ParserStats& MessageParser::get_stats() const { return stats_; }
void MessageParser::reset_stats() { stats_ = {}; last_seq_ = 0; fill_ = 0; }
