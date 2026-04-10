#ifndef BINARY_PARSER_H
#define BINARY_PARSER_H

#include <cstdint>
#include <cstddef>

class BinaryParser {
public:
    BinaryParser();

    // Feed raw TCP data
    void on_data(const char* data, size_t len);

private:
    static const size_t BUFFER_SIZE = 1 << 20; // 1MB

    char buffer[BUFFER_SIZE];
    size_t write_pos;
    size_t read_pos;

    uint32_t last_seq;

private:
    void process_buffer();
    bool parse_message(const char* msg, size_t len);

    uint32_t compute_checksum(const char* data, size_t len);
    size_t get_message_size(uint16_t msg_type);
};

#endif