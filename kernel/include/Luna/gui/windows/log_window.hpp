#pragma once

#include <Luna/gui/gui.hpp>
#include <Luna/gui/controls/items.hpp>
#include <Luna/gui/controls/text_box.hpp>

#include <Luna/misc/log.hpp>
#include <Luna/misc/font.hpp>

namespace gui {
    struct LogWindow final : public Window<controls::HStack<controls::TextBox, controls::VStack<controls::Button, controls::Button, controls::ScrollBar, controls::Button, controls::Button>>>, public log::Logger {
        LogWindow(Vec2i size_chars, const char* title): Window{new controls::HStack {
            controls::TextBox{size_chars},
            controls::VStack {
                controls::Button{read_image("A:/luna/assets/toparrow.bmp").value(), read_image("A:/luna/assets/toparrow_clicked.bmp").value(), [](void* userptr) { auto& self = *(LogWindow*)userptr; self.root->get<0>().seek(controls::TextBox::SeekMode::Start); self.update_scrollbar(); }, this},
                controls::Button{read_image("A:/luna/assets/uparrow.bmp").value(), read_image("A:/luna/assets/uparrow_clicked.bmp").value(), [](void* userptr) { auto& self = *(LogWindow*)userptr; self.scroll(-1); }, this},
                controls::ScrollBar{{16, size_chars.y * font::height - 4 * 16}, {137, 7, 52}},
                controls::Button{read_image("A:/luna/assets/downarrow.bmp").value(), read_image("A:/luna/assets/downarrow_clicked.bmp").value(), [](void* userptr) { auto& self = *(LogWindow*)userptr; self.scroll(1); }, this},
                controls::Button{read_image("A:/luna/assets/bottomarrow.bmp").value(), read_image("A:/luna/assets/bottomarrow_clicked.bmp").value(), [](void* userptr) { auto& self = *(LogWindow*)userptr; self.root->get<0>().seek(controls::TextBox::SeekMode::End); self.update_scrollbar(); }, this},
                
            }
        }, title}, size_chars{size_chars} {
            root->resize(get_canvas());
        }

        void putc(const char c) { root->get<0>().putc(c); }
        void flush() { root->get<0>().update(); update_scrollbar(); request_redraw(); }

        void handle_keyboard_op(KeyOp op, KeyCodes code) override {
            if(op == KeyOp::Unpress)
                return;

            using enum KeyCodes;
            switch (code) {
                case UpArrow: scroll(-1); break;
                case DownArrow: scroll(1); break;
                case PageUp: scroll(-25); break;
                case PageDown: scroll(25); break;
                default: break;
            }
        }

        private:
        void scroll(int64_t delta) {
            root->get<0>().seek(controls::TextBox::SeekMode::Diff, delta);
            update_scrollbar();
        }

        void update_scrollbar() {
            auto& textbox = root->get<0>();
            root->get<1>().get<2>().update(textbox.n_lines(), size_chars.y, textbox.current_line());
        }
        Vec2i size_chars;
    };
} // namespace gui
