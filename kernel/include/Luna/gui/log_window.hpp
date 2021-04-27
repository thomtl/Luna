#pragma once

#include <Luna/gui/gui.hpp>
#include <Luna/gui/basic.hpp>
#include <Luna/misc/log.hpp>

namespace gui {
    struct Log : public Widget {
        Log(Vec2i pos, Vec2i size_chars): offset{0}, curr_x{0}, curr_y{0}, pos{pos}, size_chars{size_chars}, fg{255, 255, 255}, bg{0, 0, 0}, colour_intensity{0, 0, 0} { }

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
                    } else if(buf[off] == 0x1B) { // ANSI Escape
                        off++;
                        ASSERT(buf[off++] == '[');
                        auto is_param = [](char c) -> bool { return (c >= 0x30) && (c <= 0x3F); };
                        auto is_inter = [](char c) -> bool { return (c >= 0x20) && (c <= 0x2F); };
                        auto is_final = [](char c) -> bool { return (c >= 0x40) && (c <= 0x7E); };
                        
                        std::vector<int64_t> params;
                        while(is_param(buf[off])) {
                            params.push_back(atoi(buf.data() + off));
                            while(isdigit(buf[off]))
                                off++;

                            if(buf[off] == ';') // Seperator
                                off++;
                        }

                        while(is_inter(buf[off++]))
                            ;

                        char op = buf[off - 1];
                        ASSERT(is_final(op));
                        if(op == 'm') {
                            for(auto p : params) {
                                if(p == 0) {
                                    fg = Colour{255, 255, 255};
                                    bg = Colour{0, 0, 0};
                                    colour_intensity = Colour{0, 0, 0};
                                } else if(p == 1) {
                                    colour_intensity = Colour{10, 10, 10};
                                } else if(p >= 30 && p <= 37) {
                                    constexpr Colour list[] = {
                                        {0, 0, 0},
                                        {170, 0, 0},
                                        {0, 170, 0},
                                        {170, 85, 0},
                                        {0, 0, 170},
                                        {170, 0, 170},
                                        {0, 170, 170},
                                        {170, 170, 170}
                                    };

                                    fg = list[p - 30];
                                } else {
                                    print("gui::log: Unknown ANSI m param: {}\n", p);
                                }
                            }
                        } else {
                            print("gui::log:Unknown ANSI Escape op {:c}\n", op);
                        }
                    } else {
                        desktop.put_char(eff_pos + Vec2i{(int32_t)(8 * x), (int32_t)(16 * y)}, buf[off], fg + colour_intensity, bg);
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

        Colour fg, bg, colour_intensity;
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
