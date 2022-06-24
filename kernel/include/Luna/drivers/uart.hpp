#pragma once

#include <Luna/common.hpp>
#include <Luna/cpu/pio.hpp>

#include <Luna/misc/log.hpp>

namespace uart {
    constexpr uint16_t com1_base = 0x3F8;
    constexpr uint16_t com2_base = 0x2F8;

    struct Writer final : public log::Logger {
        Writer(uint16_t base): base{base} {
            pio::outb(base + 3, 3); // Configure Line Control
            pio::outb(base + 1, 0); // Disable IRQs

            uint16_t divisor = 3;
            pio::outb(base + 3, pio::inb(base + 3) | (1 << 7)); // Set DLAB
            pio::outb(base, (divisor >> 8) & 0xFF);
            pio::outb(base, divisor & 0xFF);
            pio::outb(base + 3, pio::inb(base + 3) & ~(1 << 7)); // Clear DLAB
        }

        void putc(const char c) {
            while((pio::inb(base + 5) & (1 << 5)) == 0)
                ;

            pio::outb(base, c);
        }

        private:
        uint16_t base;
    };
} // namespace uart
