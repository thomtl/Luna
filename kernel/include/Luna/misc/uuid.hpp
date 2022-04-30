#pragma once

#include <Luna/common.hpp>
#include <std/span.hpp>


struct UUID {
    static constexpr size_t uuid_size = 16;

    UUID(const char* str) {
        constexpr uint8_t mapping[] = {6,4,2,0,11,9,16,14,19,21,24,26,28,30,32,34};
        auto ascii_to_hex = [](int c) -> uint8_t {
            if(c <= '9')
                return (uint8_t)(c - '0');
            else if(c <= 'F')
                return (uint8_t)(c - 0x37);
            else
                return (uint8_t)(c - 0x57);
        };

        for(size_t i = 0; i < uuid_size; i++)
            _buf[i] = (ascii_to_hex(str[mapping[i]]) << 4) | ascii_to_hex(str[mapping[i] + 1]);
    }

    std::span<uint8_t, uuid_size> span() {
        return std::span<uint8_t, uuid_size>(_buf, uuid_size);
    }

    std::span<const uint8_t, uuid_size> span() const {
        return std::span<const uint8_t, uuid_size>(_buf, uuid_size);
    }

    private:
    uint8_t _buf[uuid_size];
};