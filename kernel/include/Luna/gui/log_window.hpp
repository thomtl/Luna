#pragma once

#include <Luna/gui/gui.hpp>
#include <Luna/gui/basic.hpp>
#include <Luna/misc/log.hpp>

namespace gui {
    struct Log : public Widget {
        Log(Vec2i pos, Vec2i size_chars): offset{0}, curr_x{0}, curr_y{0}, pos{pos}, size_chars{size_chars} { }

        void redraw(Desktop& desktop, const Vec2i& parent_pos) {
            auto eff_pos = parent_pos + pos;

            for(int32_t y = 0; y < (size_chars.y * 16); y++)
                for(int32_t x = 0; x < (size_chars.x * 8); x++)
                    desktop.put_pixel(eff_pos + Vec2i{x, y}, Colour{0, 0, 0});

            size_t off = offset;
            for(int32_t y = 0; y < size_chars.y; y++) {
                for(int32_t x = 0; x < size_chars.x; x++) {
                    if(off >= buf.size())
                        goto stop;
                
                    if(buf[off] == '\r') {
                        off++;
                    } else if(buf[off] == '\n') {
                        if(x != (size_chars.x - 1)) {
                            off++;
                            break;
                        }
                    } else {
                        desktop.put_char(eff_pos + Vec2i{(int32_t)(8 * x), (int32_t)(16 * y)}, buf[off], Colour(255, 255, 255), Colour(0, 0, 0));
                        off++;
                    }
                }
            }

            stop: ;
        }

        void putc(const char c) {
            if(c == '\t') {
                putc(' ');
                buf.push_back(' ');
            } else {
                buf.push_back(c);
            }
        
            curr_x++;
            if(curr_x == (uint32_t)size_chars.x || c == '\n') {
                curr_y++;
                if(curr_y == (uint32_t)size_chars.y) {
                    curr_y--;

                    size_t off = 0;
                    while(buf[offset + off] != '\n' && off < (uint32_t)(size_chars.x - 1))
                        off++;

                    offset += off + 1;
                }

                curr_x = 0;
            }     
        }

        private:
        std::vector<char> buf;
        size_t offset;
        size_t curr_x, curr_y;
        Vec2i pos, size_chars;
    };

    struct LogWindow : public Widget, public log::Logger {
        LogWindow(const char* title): window{{25, 30}, {400, 700}, title}, logger{{4, 8}, {49, 42}} {
            window.add_widget(&logger);
        }

        void putc(const char c) const {
            logger.putc(c);
        }

        void flush() const {
            gui::get_desktop().update();
        }

        void redraw(Desktop& desktop, const Vec2i& parent_pos) {
            window.redraw(desktop, parent_pos);
        }

        private:
        mutable Window window;
        mutable Log logger;
    };
} // namespace gui
