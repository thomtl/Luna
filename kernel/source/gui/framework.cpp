#include <Luna/gui/framework.hpp>

void gui::draw::rect(Canvas& canvas, Vec2i pos, Vec2i size, Colour colour) {
    for(int32_t x = 0; x < size.x; x++)
        for(int32_t y = 0; y < size.y; y++)
            canvas.put_pixel(pos + Vec2i{x, y}, colour);
}

void gui::draw::rect(Canvas& canvas, Rect rect, Colour colour) {
    draw::rect(canvas, rect.pos, rect.size, colour);
}

void gui::draw::text(Canvas& canvas, Vec2i pos, const char* text, Colour fg, Colour bg, TextAlign align) {
    auto size = strlen(text);
    if(align == TextAlign::Center)
        pos.x -= (size * 8) / 2;

    for(size_t i = 0; i < size; i++)
        canvas.put_char(pos + Vec2i{(int32_t)(8 * i), 0}, text[i], fg, bg);
}