#pragma once

#include <Luna/gui/gui.hpp>
#include <Luna/gui/basic.hpp>

namespace gui {
    struct Framebuffer : public Widget {
        Framebuffer(Vec2i pos, Vec2i size): pos{pos}, size{size} { 
            fb.resize(size.x * size.y);
        }

        void redraw(Desktop& desktop, const Vec2i& parent_pos) {
            for(int32_t y = 0; y < size.y; y++)
                for(int32_t x = 0; x < size.x; x++)
                    desktop.put_pixel(parent_pos + pos + Vec2i{x, y}, Colour{fb[x + y * size.y]});
        }

        std::vector<uint32_t> fb;
        private:
        Vec2i pos, size;
    };

    struct FbWindow : public Widget {
        FbWindow(Vec2i size, const char* title): window{{450, 30}, {size.x + 10, size.y + 10}, title}, fb{{4, 8}, size} {
            window.add_widget(&fb);
        }

        void redraw(Desktop& desktop, const Vec2i& parent_pos) {
            window.redraw(desktop, parent_pos);
        }

        uint32_t* get_fb() {
            return fb.fb.data();
        }

        private:
        mutable Window window;
        mutable Framebuffer fb;
    };
} // namespace gui
