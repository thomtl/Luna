#pragma once

#include <Luna/common.hpp>
#include <Luna/cpu/pio.hpp>

namespace e9 {
    constexpr uint16_t port_addr = 0xe9;
    constexpr uint16_t expected_value = 0xe9;

    struct Writer {
        void putc(const char c) const {
            pio::outb(port_addr, c);
        }
    };
} // namespace e9
