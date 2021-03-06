#pragma once

#include <Luna/gui/gui.hpp>
#include <Luna/gui/basic.hpp>

namespace gui {
    struct Framebuffer : public Widget {
        Framebuffer(Vec2i pos, Vec2i size, uint8_t* fb): fb{(uint32_t*)fb}, pos{pos}, size{size} { }

        void redraw(Desktop& desktop, const Vec2i& parent_pos) {
            for(int32_t y = 0; y < size.y; y++)
                for(int32_t x = 0; x < size.x; x++)
                    desktop.put_pixel(parent_pos + pos + Vec2i{x, y}, Colour{fb[x + y * size.y]});
        }

        private:
        uint32_t* fb;
        Vec2i pos, size;
    };

    struct FbWindow : public Widget {
        FbWindow(Vec2i size, uint8_t* fb, const char* title): window{{450, 30}, {size.x + 10, size.y + 10}, title}, fb{{4, 8}, size, fb} {
            window.add_widget(&this->fb);
        }

        void redraw(Desktop& desktop, const Vec2i& parent_pos) {
            window.redraw(desktop, parent_pos);
        }

        private:
        mutable Window window;
        mutable Framebuffer fb;
    };
} // namespace gui
