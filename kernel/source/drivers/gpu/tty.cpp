#include <Luna/drivers/gpu/tty.hpp>
#include <Luna/misc/font.hpp>

#include <std/string.hpp>

tty::Writer::Writer() {

}

void tty::Writer::putc(const char c) {
    auto& gpu = gpu::get_gpu();
    auto mode = gpu.get_mode();
    auto screen_height = mode.height / font::height;
    auto screen_width = mode.width / font::width;
    auto screen_pitch = mode.pitch / 4;
    
    auto print = [&]() {
        auto fb = gpu.get_fb();
        auto* line = (uint32_t*)fb.data() + y * font::height * screen_pitch + x * font::width;

        auto dc = (c >= 32) ? c : 127;
        for(uint8_t i = 0; i < font::height; i++) {
            auto* dest = line;
            auto bits = font::bitmap[(dc - 32) * font::height + i];
            for(uint8_t j = 0; j < font::width; j++) {
                auto bit = (1 << ((font::width - 1) - j));
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
        auto fb = gpu.get_fb();

        for(size_t i = 1; i < screen_height; i++) {
            auto* dst = (uint32_t*)fb.data() + ((i - 1) * screen_pitch * font::height);
            auto* src = (uint32_t*)fb.data() + (i * screen_pitch * font::height);
            memcpy(dst, src, mode.pitch * font::height);
        }

        memset((uint32_t*)fb.data() + ((screen_height - 1) * screen_pitch * font::height), 0, mode.pitch * font::height);

        x = 0;
        y = screen_height;
    };
    
    if(c == '\n') {
        x = 0;
        y++;
    } else if(c == '\r') {
        x = 0;  
    } else if(c == '\t') {
        x += 4;     
    } else {
        print();
        x++;
    }

    // Handle scrolling
    if(x >= screen_width) {
        y++;
        x = 0;        
    }

    if(y >= screen_height) {
        scroll();
        y--;
    }
}

void tty::Writer::flush() {
    gpu::get_gpu().flush();
}