#pragma once

#include <Luna/gui/gui.hpp>

namespace gui::draw {
    void rect(Canvas& canvas, Vec2i pos, Vec2i size, Colour colour);
    void rect(Canvas& canvas, Rect rect, Colour colour);

    enum class TextAlign { None, Center };
    void text(Canvas& canvas, Vec2i pos, const char* text, Colour fg = Colour(255, 255, 255), Colour bg = Colour(0, 0, 0), TextAlign align = TextAlign::None);

    /*struct Window : public Widget {
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
    };*/
} // namespace gui
