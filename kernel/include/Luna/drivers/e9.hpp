#pragma once

#include <Luna/common.hpp>
#include <Luna/cpu/pio.hpp>

#include <Luna/misc/log.hpp>

namespace e9 {
    constexpr uint16_t port_addr = 0xe9;
    constexpr uint16_t expected_value = 0xe9;

    struct Writer final : public log::Logger {
        void putc(const char c) {
            pio::outb(port_addr, c);
        }

        void puts(const char* str) {
            while(*str)
                pio::outb(port_addr, *str++);
        }
    };
} // namespace e9
