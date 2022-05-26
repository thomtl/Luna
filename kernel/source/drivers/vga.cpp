#include <Luna/drivers/vga.hpp>

void vga::Writer::putc(const char c) {
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
        size_t index = y * screen_width + x;

        auto& v = fb[index];
        v = c | (0x07 << 8);

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

void vga::Writer::scroll() {
    for(size_t i = 1; i < screen_height; i++) {
        auto* dst = fb + ((i - 1) * screen_width);
        auto* src = fb + (i * screen_width);
        memcpy(dst, src, screen_width * sizeof(uint16_t));
    }

    for(size_t i = 0; i < screen_width; i++) {
        size_t index = (screen_height - 1) * screen_width + i;

        fb[index] = ' ' | (0x07 << 8);
    }

    x = 0;
    y = screen_height;
}