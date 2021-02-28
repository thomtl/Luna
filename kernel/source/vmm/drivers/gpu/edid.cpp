#include <Luna/vmm/drivers/gpu/edid.hpp>

vm::gpu::edid::Edid vm::gpu::edid::generate_edid(const GpuInfo& info) {
    Edid edid{};

    edid.header[0] = 0x00;
    edid.header[1] = 0xFF;
    edid.header[2] = 0xFF;
    edid.header[3] = 0xFF;
    edid.header[4] = 0xFF;
    edid.header[5] = 0xFF;
    edid.header[6] = 0xFF;
    edid.header[7] = 0x0;

    edid.product = 0x1234;
    edid.serial = 0x5678'ABCD;

    edid.week = 0xFF; // Model year flag
    edid.year = 31; // 1990 + 31 = 2021,

    edid.ver_major = 1; edid.ver_minor = 4;

    edid.input_params = (1 << 7) | (0b010 << 4) | 0b0101; // Digital, 8bit Colour Depth, DisplayPort

    edid.detail_timings[0].pixel_clock = (75 * info.native_x * info.native_y) / 10000; // Something that looks sane-ish as pixelclock
    edid.detail_timings[0].horz_active = info.native_x & 0xFF;
    edid.detail_timings[0].horz_active_blank_msb = ((info.native_x >> 8) << 4);

    edid.detail_timings[0].vert_active = info.native_y & 0xFF;
    edid.detail_timings[0].vert_active_blank_msb = ((info.native_y >> 8) << 4);

    uint8_t checksum = 0;
    for(size_t i = 0; i < sizeof(Edid); i++)
        checksum += ((uint8_t*)&edid)[i];
    edid.checksum = 256 - checksum;

    return edid;
}