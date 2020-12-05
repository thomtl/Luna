#pragma once

#include <Luna/common.hpp>
#include <Luna/cpu/pio.hpp>

#include <std/string.hpp>

namespace vga {
    constexpr uintptr_t fb_pa = 0xB8000;
    constexpr uint8_t screen_width = 80;
    constexpr uint8_t screen_height = 25;

    struct Writer {
        Writer(): fb{(uint16_t*)(fb_pa + phys_mem_map)} {}
        void putc(const char c) const;

        private:
        void scroll() const;

        static uint8_t x, y;
        uint16_t* fb;
    };
} // namespace vga
