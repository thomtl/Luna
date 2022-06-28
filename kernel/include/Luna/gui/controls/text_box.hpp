#pragma once

#include <Luna/common.hpp>
#include <Luna/gui/base.hpp>
#include <Luna/gui/controls/base.hpp>

namespace gui::controls {
    struct TextBox final : public Control {
        //TextBox(Vec2i size_chars, Colour fg = Colour{255, 255, 255}, Colour bg = Colour{0, 0, 0}): cur_offset{0}, view_offset{0}, curr_x{0}, curr_y{0}, size_chars{size_chars}, fg{fg}, bg{bg}, colour_intensity{0, 0, 0} { }
        TextBox(Vec2i size_chars, Colour fg = Colour{255, 255, 255}, Colour bg = Colour{0, 0, 0}): lines{1}, curr_line_idx{0}, view_line_idx{0}, size_chars{size_chars}, fg{fg}, bg{bg}, colour_intensity{0, 0, 0} { }

        void resize(NonOwningCanvas canvas) override {
            this->canvas = canvas;

            auto extent = preferred_size();
            ASSERT(this->canvas.size.y >= extent.y && this->canvas.size.x >= extent.x);     
        }

        Vec2i preferred_size() const override {
            return {size_chars.x * font::width + 1, size_chars.y * font::height};
        }

        enum class SeekMode { Start, End, Diff };

        void seek(SeekMode mode, int64_t diff = 0) {
            if(this->mode == Mode::Update)
                view_line_idx = curr_line_idx;

            this->mode = Mode::Free;
            using enum SeekMode;
            switch (mode) {
                case Start: view_line_idx = 0; break;
                case End: view_line_idx = lines.size() - size_chars.y; break;
                case Diff: view_line_idx += diff; break;
            }
            
            view_line_idx = clamp(view_line_idx, 0, lines.size() - size_chars.y);

            update();
        } 

        size_t n_lines() const { return lines.size(); }
        size_t current_line() const { return (mode == Mode::Update) ? curr_line_idx : view_line_idx; }

        void update() {
            canvas.clear(bg);

            size_t line_idx = current_line();
            for(int64_t y = 0; y < size_chars.y; y++) {
                if((size_t)(line_idx + y) >= lines.size())
                    break;
                
                auto& line = lines[line_idx + y];
                int64_t offset = 0;
                for(int64_t x = 0; x < size_chars.x; x++) {
                    if((size_t)(x + offset) >= line.size())
                        break;

                    if(line[x + offset] == 0x1B) { // ANSI Escape
                        auto peek = [&](int32_t off = 0) { return line[x + offset + off]; };
                        auto consume = [&] { offset++; return line[x + offset - 1]; };

                        consume();
                        ASSERT(consume() == '[');
                        auto is_param = [](char c) -> bool { return (c >= 0x30) && (c <= 0x3F); };
                        auto is_inter = [](char c) -> bool { return (c >= 0x20) && (c <= 0x2F); };
                        auto is_final = [](char c) -> bool { return (c >= 0x40) && (c <= 0x7E); };
                        
                        std::vector<int64_t> params;
                        while(is_param(peek())) {
                            params.push_back(atoi(line.data() + x + offset));
                            while(isdigit(peek()))
                                consume();

                            if(peek() == ';') // Seperator
                                consume();
                        }

                        while(is_inter(consume()))
                            ;

                        char op = peek(-1);
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
                    }

                    char c = line[x + offset];
                    canvas.put_char(Vec2i{(int32_t)(font::width * x) + 1, (int32_t)(font::height * y)}, c, fg + colour_intensity, bg);
                }
            }
        }

        void putc(char c) {
            switch (c) {
                case '\t': lines.back().push_back(' '); break;
                case '\r': return;
                case '\n': lines.emplace_back(); break;
                default: lines.back().push_back(c); break;
            }

            mode = Mode::Update;
            if(c == '\n' && lines.size() > (size_t)size_chars.y)
                curr_line_idx++;
        }







        private:
        std::vector<std::vector<char>> lines;
        int64_t curr_line_idx, view_line_idx;

        Vec2i size_chars;

        Colour fg, bg, colour_intensity;

        NonOwningCanvas canvas;
        enum class Mode { Update, Free };
        Mode mode;

        /*void move_line(int64_t diff) {
            if(mode == Mode::Update)
                view_offset = cur_offset;

            mode = Mode::Free;
            ASSERT(diff == -1 || diff == 1); // TODO

            if(diff == 1) {
                while(buf[view_offset] != '\n') {
                    view_offset++;
                    if(view_offset >= buf.size())
                        break;
                }
            } else {
                view_offset--;
                while(buf[view_offset] != '\n') {
                    if(view_offset == 0)
                        break;
                    view_offset--;
                }
            }

            update();
        }

        void update() {
            canvas.clear(bg);

            size_t off = (mode == Mode::Update) ? cur_offset : view_offset;
            for(int32_t y = 0; y < size_chars.y; y++) {
                for(int32_t x = 0; x < size_chars.x; x++) {
                    if(off >= buf.size())
                        return;
                
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
                        canvas.put_char(Vec2i{(int32_t)(font::width * x) + 1, (int32_t)(font::height * y)}, buf[off], fg + colour_intensity, bg);
                        off++;
                    }
                }
            }
        }

        void putc(char c) {
            if(c == '\t') {
                putc(' ');
                buf.push_back(' ');
            } else {
                buf.push_back(c);
            }

            mode = Mode::Update;
            curr_x++;
            if(curr_x == (uint32_t)size_chars.x || c == '\n') {
                curr_y++;
                if(curr_y == (uint32_t)size_chars.y) {
                    curr_y--;

                    size_t off = 0;
                    while(buf[cur_offset + off] != '\n' && off < (uint32_t)(size_chars.x - 1))
                        off++;

                    cur_offset += off + 1;
                }

                curr_x = 0;
            }     
        }

        private:
        std::vector<char> buf;
        size_t cur_offset, view_offset;
        size_t curr_x, curr_y;
        Vec2i size_chars;

        Colour fg, bg, colour_intensity;

        NonOwningCanvas canvas;

        enum class Mode { Update, Free };
        Mode mode;*/
    };    
} // namespace gui::controls


