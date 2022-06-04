#include <Luna/gui/framework.hpp>
#include <std/algorithm.hpp>

void gui::draw::rect(gui::NonOwningCanvas& canvas, Vec2i pos, Vec2i size, Colour colour) {
    for(int64_t y = 0; y < size.y; y++)
        std::fill_n(canvas.fb.data() + pos.x + (pos.y + y) * canvas.pitch, size.x, colour);
}

void gui::draw::rect(gui::NonOwningCanvas& canvas, Rect rect, Colour colour) {
    draw::rect(canvas, rect.pos, rect.size, colour);
}

void gui::draw::text(gui::NonOwningCanvas& canvas, Vec2i pos, const char* text, Colour fg, Colour bg, TextAlign align) {
    auto size = strlen(text);
    if(align == TextAlign::Center)
        pos.x -= (size * 8) / 2;

    for(size_t i = 0; i < size; i++)
        canvas.put_char(pos + Vec2i{(int32_t)(8 * i), 0}, text[i], fg, bg);
}