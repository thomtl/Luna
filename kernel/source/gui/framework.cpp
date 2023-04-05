#include <Luna/gui/framework.hpp>
#include <std/algorithm.hpp>

void gui::draw::rect(gui::NonOwningCanvas& canvas, Vec2i pos, Vec2i size, Colour colour) {
    if((pos.x + size.x) > canvas.size.x)
        size.x = canvas.size.x - pos.x;

    for(int64_t y = pos.y; y < (pos.y + size.y) && y < canvas.size.y; y++)
        std::fill_n(canvas.fb.data() + pos.x + y * canvas.pitch, size.x, colour);
}

void gui::draw::rect(gui::NonOwningCanvas& canvas, Rect rect, Colour colour) {
    draw::rect(canvas, rect.pos, rect.size, colour);
}

void gui::draw::text(gui::NonOwningCanvas& canvas, Vec2i pos, const char* text, Colour fg, Colour bg, TextAlign align) {
    auto size = strlen(text);
    if(align == TextAlign::Center)
        pos.x -= (size * font::width) / 2;

    int32_t limit = min(canvas.size.x, pos.x + size * 8);
    for(int32_t x_off = pos.x; x_off < limit; x_off += 8)
        canvas.put_char({x_off, pos.y}, *text++, fg, bg);
}