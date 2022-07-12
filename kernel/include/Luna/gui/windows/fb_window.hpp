#pragma once

#include <Luna/gui/gui.hpp>
#include <Luna/gui/framework.hpp>

namespace gui {
    struct FbWindow final : public RawWindow {
        FbWindow(Vec2i size, uint8_t* fb, const char* title, void (*key_handler)(void*, KeyOp, KeyCodes), void* userptr): RawWindow{size, title}, size{size}, fb{(uint32_t*)fb}, key_handler{key_handler}, userptr{userptr} { }

        void update() {
            for(int64_t y = 0; y < size.y; y++) {
                for(int64_t x = 0; x < size.x; x++) {
                    auto off = y * canvas.size.x + x;
                    canvas.fb[off] = Colour{fb[off], 255};
                }
            }

            request_redraw();
        }

        void handle_keyboard_op(KeyOp op, KeyCodes code) override {
            if(key_handler)
                key_handler(userptr, op, code);
        }

        private:
        Vec2i size;
        uint32_t* fb;
        void (*key_handler)(void*, KeyOp, KeyCodes);
        void* userptr;
    };
} // namespace gui
