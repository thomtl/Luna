#include <Luna/gui/bmp_parser.hpp>

std::optional<gui::Image> gui::bmp_parser::parse_bmp(vfs::File* file) {
    FileHeader file_header{};

    ASSERT(file->read(0, sizeof(FileHeader), (uint8_t*)&file_header) == sizeof(FileHeader));
    ASSERT(file_header.id == 0x4D42);

    DIBHeader header{};
    ASSERT(file->read(sizeof(FileHeader), sizeof(DIBHeader), (uint8_t*)&header) == sizeof(DIBHeader));
    ASSERT(header.header_size == 40 || header.header_size == 124); // Both are compatible
    ASSERT(header.planes == 1);

    ASSERT(header.width > 0);
    ASSERT(header.height > 0);

    ASSERT(header.bpp == 32);

    Image image{{header.width, header.height}};
    if(header.compression_type == 3) {
        auto row_size = div_ceil(header.bpp * header.width, 32) * 4;
        auto pixel_array_size = row_size * header.height;

        auto* buf = new uint8_t[pixel_array_size];
        ASSERT(file->read(file_header.pixel_offset, pixel_array_size, buf) == pixel_array_size);

        auto* pixel_array = (uint32_t*)buf;
        auto target = image.span();

        for(int64_t y = 0; y < header.height; y++) {
            size_t inv_y = header.height - y - 1;

            auto off = y * header.width;
            auto inv_off = inv_y * header.width;

            for(int64_t x = 0; x < header.width; x++) {
                target[off + x] = pixel_array[inv_off + x];
            }
        }

        delete[] buf;
    } else {
        print("bmp: Unknown compression type: {}\n", uint32_t{header.compression_type});
        
        return std::nullopt;
    }

    return image;
}