#pragma once

#include <Luna/common.hpp>
#include <Luna/cpu/pio.hpp>

#include <Luna/misc/log.hpp>
#include <std/string.hpp>

namespace vga {
    constexpr uintptr_t fb_pa = 0xB8000;
    constexpr uint8_t screen_width = 80;
    constexpr uint8_t screen_height = 25;

    struct Writer final : public log::Logger {
        Writer(): fb{(uint16_t*)(fb_pa + phys_mem_map)} {}
        void putc(const char c);

        private:
        void scroll();

        uint8_t x, y;
        uint16_t* fb;
    };
} // namespace vga
