#pragma once

#include <Luna/common.hpp>

namespace vm::gpu::edid {
    struct [[gnu::packed]] Edid {
        uint8_t header[8];
        uint16_t manufacturer, product;
        uint32_t serial;
        uint8_t week, year;
        uint8_t ver_major, ver_minor;
        uint8_t input_params, hor_size, ver_size, gamma, features_bitmap;
        uint8_t colour_lsb, bw_msb, r_msb, r_y_msb;
        uint16_t g_msb, b_msb, white_point;
        uint8_t timing_bitmap[3];
        struct {
            uint8_t x_res;
            uint8_t aspect;   
        } timings[8];
        struct {
            uint16_t pixel_clock;
            uint8_t horz_active;
            uint8_t horz_blank;
            uint8_t horz_active_blank_msb;
            uint8_t vert_active;
            uint8_t vert_blank;
            uint8_t vert_active_blank_msb;
            uint8_t horz_sync_offset;
            uint8_t horz_sync_pulse;
            uint8_t vert_sync;
            uint8_t sync_msb;
            uint8_t dimension_width;
            uint8_t dimension_height;
            uint8_t dimension_msb;
            uint8_t horz_border;
            uint8_t vert_border;
            uint8_t features;
        } detail_timings[4];
        uint8_t n_extensions;
        uint8_t checksum;
    };
    static_assert(sizeof(Edid) == 128);

    struct GpuInfo {
        size_t native_x, native_y;
    };

    Edid generate_edid(const GpuInfo& info);
} // namespace vm::gpu::edid
