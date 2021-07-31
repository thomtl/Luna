#pragma once

#include <Luna/gui/gui.hpp>
#include <Luna/gui/framework.hpp>

namespace gui {
    struct FbWindow : public Window {
        FbWindow(Vec2i size, uint8_t* fb, const char* title): Window{size, title}, size{size}, fb{(uint32_t*)fb} { }

        void update() {
            for(int32_t y = 0; y < size.y; y++) {
                for(int32_t x = 0; x < size.x; x++) {
                    auto off = y * canvas.size.x + x;
                    canvas.fb[off] = Colour{fb[off], 255};
                }
            }
        }

        private:
        Vec2i size;
        uint32_t* fb;
    };
} // namespace gui
