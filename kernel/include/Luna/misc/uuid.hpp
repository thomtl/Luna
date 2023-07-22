#pragma once

#include <Luna/common.hpp>
#include <std/span.hpp>
#include <std/string.hpp>


struct UUID {
    static constexpr size_t uuid_size = 16;
    static constexpr uint8_t string_mapping[] = {6,4,2,0,11,9,16,14,19,21,24,26,28,30,32,34};

    constexpr UUID(const char* str) {
        constexpr auto ascii_to_hex = [](int c) -> uint8_t {
            if(c <= '9')
                return (uint8_t)(c - '0');
            else if(c <= 'F')
                return (uint8_t)(c - 0x37);
            else
                return (uint8_t)(c - 0x57);
        };

        for(size_t i = 0; i < uuid_size; i++)
            _buf[i] = (ascii_to_hex(str[string_mapping[i]]) << 4) | ascii_to_hex(str[string_mapping[i] + 1]);
    }

    UUID(std::span<uint8_t, uuid_size> uuid) {
        memcpy(_buf, uuid.data(), uuid_size);
    }

    const char* to_string(char buf[37]) const {
        buf[8] = buf[13] = buf[18] = buf[23] = '-';


        constexpr auto hex_to_ascii = [](uint8_t v) -> uint8_t {
            if(v <= 9)
                return (uint8_t)(v + '0');
            else
                return (uint8_t)((v - 10) + 'A');
        };

        for(size_t i = 0; i < 16; i++) {
            buf[string_mapping[i]] = hex_to_ascii((_buf[i] >> 4) & 0xF);
            buf[string_mapping[i] + 1] = hex_to_ascii(_buf[i] & 0xF);
        }

        return buf;
    }

    std::span<uint8_t, uuid_size> span() {
        return std::span<uint8_t, uuid_size>(_buf, uuid_size);
    }

    std::span<const uint8_t, uuid_size> span() const {
        return std::span<const uint8_t, uuid_size>(_buf, uuid_size);
    }

    bool operator==(const UUID& other) const {
        return memcmp(_buf, other._buf, 16) == 0;
    }

    private:
    uint8_t _buf[uuid_size];
};