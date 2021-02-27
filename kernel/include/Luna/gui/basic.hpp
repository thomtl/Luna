#pragma once

#include <Luna/gui/gui.hpp>

namespace gui {
    struct Rect : public Widget {
        Rect(Vec2i pos, Vec2i size, Colour colour): pos{pos}, size{size}, colour{colour} {}

        void redraw(Desktop& desktop, const Vec2i& parent_pos) {
            auto eff_pos = parent_pos + pos;

            for(int32_t x = 0; x < size.x; x++)
                for(int32_t y = 0; y < size.y; y++)
                    desktop.put_pixel(eff_pos + Vec2i{x, y}, colour);
        }

        private:
        Vec2i pos, size;
        Colour colour;
    };

    struct Text : public Widget {
        enum class Align { None, Center };
        Text(Vec2i pos, const char* text, Colour fg = Colour(255, 255, 255), Colour bg = Colour(0, 0, 0), Align align = Align::None): pos{pos}, text{text}, size{strlen(text)}, align{align}, fg{fg}, bg{bg} {
            if(align == Align::Center)
                this->pos.x -= (size * 8) / 2;
        }

        void redraw(Desktop& desktop, const Vec2i& parent_pos) {
            auto eff_pos = parent_pos + pos;

            for(size_t i = 0; i < size; i++)
                desktop.put_char(eff_pos + Vec2i{(int32_t)(8 * i), 0}, text[i], fg, bg);
        }

        private:
        Vec2i pos;
        const char* text;
        size_t size;
        Align align;

        Colour fg, bg;
    };

    struct Window : public Widget {
        Window(Vec2i pos, Vec2i size, const char* title): pos{pos}, size{size} {
            constexpr int32_t width = 2;
            constexpr auto colour = Colour(200, 200, 200);

            widgets.push_back(new Rect{{0, 0}, {size.x, width}, colour});
            widgets.push_back(new Rect{{0, size.y - width}, {size.x, width}, colour});

            // These don't need to draw over the previous ones, so offset them a bit
            widgets.push_back(new Rect{{0, width}, {width, size.y - width}, colour});
            widgets.push_back(new Rect{{size.x - width, width}, {width, size.y - width}, colour});

            widgets.push_back(new Text{{size.x / 2, -7}, title, Colour{255, 255, 255}, Colour{0, 0, 0}, Text::Align::Center});
        }

        void redraw(Desktop& desktop, const Vec2i& parent_pos) {
            for(auto& widget : widgets)
                widget->redraw(desktop, parent_pos + pos);
        }

        void add_widget(Widget* widget) { widgets.push_back(widget); }
        std::vector<Widget*> widgets;

        private:
        Vec2i pos, size;
    };
} // namespace gui
