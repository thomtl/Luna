#pragma once

#include <Luna/gui/gui.hpp>
#include <Luna/gui/framework.hpp>

namespace gui {
    struct FbWindow final : public RawWindow {
        FbWindow(Vec2i size, uint8_t* fb, const char* title, void (*key_handler)(void*, KeyOp, KeyCodes), void* userptr): RawWindow{size, title}, size{size}, fb{(uint32_t*)fb}, key_handler{key_handler}, userptr{userptr} { }

        void update() {
            memcpy(canvas.fb.data(), fb, size.x * size.y * sizeof(uint32_t));

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
