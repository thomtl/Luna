#pragma once

#include <Luna/gui/gui.hpp>

namespace gui::draw {
    void rect(Canvas& canvas, Vec2i pos, Vec2i size, Colour colour);
    void rect(Canvas& canvas, Rect rect, Colour colour);

    enum class TextAlign { None, Center };
    void text(Canvas& canvas, Vec2i pos, const char* text, Colour fg = Colour(255, 255, 255), Colour bg = Colour(0, 0, 0), TextAlign align = TextAlign::None);

    struct TextWriter : public log::Logger {
        TextWriter(Canvas& canvas, Vec2i pos, Colour fg = Colour(255, 255, 255), Colour bg = Colour(0, 0, 0)): _canvas(&canvas), _pos{pos}, _fg{fg}, _bg{bg} {}

        void putc(const char c) {
            _canvas->put_char(_pos, c, _fg, _bg);
            _pos += Vec2i{8, 0}; // TODO
        }

        private:
        Canvas* _canvas;
        Vec2i _pos;
        Colour _fg, _bg;
    };
} // namespace gui
