#pragma once

#include <Luna/common.hpp>
#include <Luna/fs/vfs.hpp>
#include <Luna/gui/base.hpp>

namespace gui::bmp_parser {
    struct [[gnu::packed]] FileHeader {
        uint16_t id;
        uint32_t filesize;
        uint32_t reserved;
        uint32_t pixel_offset;
    };

    struct [[gnu::packed]] DIBHeader {
        uint32_t header_size;
        int32_t width;
        int32_t height;
        uint16_t planes;
        uint16_t bpp;
        uint32_t compression_type;
        uint32_t image_size;
        uint32_t horizontal_res;
        uint32_t vertical_res;
        uint32_t palette_size;
        uint32_t important_colours_used;
    };

    std::optional<Image> parse_bmp(vfs::File* file);
} // namespace gui::bmp_parser
