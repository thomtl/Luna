#pragma once

#include <Luna/common.hpp>
#include <Luna/drivers/gpu/gpu.hpp>

namespace vbe {
    struct [[gnu::packed]] VBEFarPtr {
        uint16_t off;
        uint16_t seg;

        uint32_t phys() const {
            return (seg << 4) + off;
        }
    };

    struct [[gnu::packed]] VBEInfoBlock {
        uint8_t signature[4];
        uint8_t minor_ver;
        uint8_t major_ver;
        VBEFarPtr oem;
        uint32_t cap;
        VBEFarPtr mode_info;
        uint16_t total_mem;
        uint16_t software_rev;
        VBEFarPtr vendor;
        VBEFarPtr product_name;
        VBEFarPtr product_rev;
        uint8_t reserved[222];
        uint8_t oem_data[256];
    };

    struct [[gnu::packed]] VBEModeInfoBlock {
        uint16_t attr;
        uint8_t win_a, win_b;
        uint16_t granularity, win_size, seg_a, seg_b;
        uint32_t win_func_ptr;
        uint16_t pitch, width, height;
        uint8_t w_char, h_char;
        uint8_t planes, bpp, banks, mmodel, bank_size, image_pages, reserved, red_mask, red_pos, green_mask, green_pos, blue_mask, blue_pos, reserved_mask, reserved_pos, colour_attr;
        uint32_t fb;
        uint32_t off_screen_mem_off;
        uint16_t off_screen_mem_size;
        uint8_t reserved_0[206];
    };

    void init();
    gpu::Mode set_mode(std::pair<uint16_t, uint16_t> res, uint8_t bpp);
} // namespace vbe
