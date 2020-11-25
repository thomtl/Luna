#pragma once

#include <Luna/common.hpp>

namespace tss {
    struct [[gnu::packed]] Table {
        uint32_t reserved;
        uint64_t rsps[3];
        uint64_t reserved_0;
        uint64_t ists[7];
        uint64_t reserved_1;
        uint16_t reserved_2;
        uint16_t io_bitmap_off;

        static void load(uint16_t sel) {
            asm volatile("ltr %0" : : "r"(sel) : "memory");
        }

        static uint16_t store() {
            uint16_t ret = 0;
            asm volatile("str %0" : "=rm"(ret) : : "memory");

            return ret;
        }
    };
} // namespace tss
