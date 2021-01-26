#include <Luna/drivers/gpu/tty.hpp>
#include <std/string.hpp>

extern uint8_t font_bitmap[];
constexpr uint8_t font_width = 8;
constexpr uint8_t font_height = 16;

tty::Writer::Writer() {

}

void tty::Writer::putc(const char c) const {
    auto& gpu = gpu::get_gpu();
    auto mode = gpu.get_mode();
    auto screen_height = mode.height / font_height;
    auto screen_width = mode.width / font_width;
    auto screen_pitch = mode.pitch / 4;
    
    auto print = [&]() {
        auto* fb = (uint32_t*)gpu.get_fb();
        auto* line = fb + y * font_height * screen_pitch + x * font_width;

        auto dc = (c >= 32) ? c : 127;
        for(uint8_t i = 0; i < font_height; i++) {
            auto* dest = line;
            auto bits = font_bitmap[(dc - 32) * font_height + i];
            for(uint8_t j = 0; j < font_width; j++) {
                auto bit = (1 << ((font_width - 1) - j));
                *dest++ = (bits & bit) ? ~0 : 0;
            }
            line += screen_pitch;
        }

        /*gpu::Rect rect{};
        rect.w = font_width;
        rect.h = font_height;

        rect.x = x * font_width;
        rect.y = y * font_width;

        gpu.flush(rect);*/
    };

    auto scroll = [&]() {
        auto* fb = (uint32_t*)gpu.get_fb();

        for(size_t i = 1; i < screen_height; i++) {
            auto* dst = fb + ((i - 1) * screen_pitch * font_height);
            auto* src = fb + (i * screen_pitch * font_height);
            memcpy(dst, src, mode.pitch * font_height);
        }

        memset(fb + ((screen_height - 1) * screen_pitch * font_height), 0, mode.pitch * font_height);

        x = 0;
        y = screen_height;
    };

    if(c == '\n') {
        x = 0;
        y++;

        if(y >= screen_height) {
            scroll();
            y--;
        }
    } else if(c == '\r') {
        x = 0;  
    } else {
        print();

        x++;
        if(x >= screen_width) {
            y++;
            x = 0;
            if(y >= screen_height) {
                scroll();
                y--;
            }
        }
    }
}

void tty::Writer::flush() const {
    gpu::get_gpu().flush();
}