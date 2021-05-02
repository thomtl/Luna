#pragma once

#include <Luna/common.hpp>
#include <Luna/drivers/gpu/gpu.hpp>
#include <Luna/cpu/threads.hpp>
#include <Luna/misc/log.hpp>

extern uint8_t font_bitmap[];

namespace gui {
    template<typename T>
    struct Vec2 {
        T x, y;

        Vec2 operator+(const Vec2& b) const {
            return {x + b.x, y + b.y};
        }
    };
    using Vec2i = Vec2<int32_t>;

    union Colour {
        constexpr Colour(uint32_t v): raw{v} {}
        constexpr Colour(uint8_t r, uint8_t g, uint8_t b): b{b}, g{g}, r{r}, a{255} {}
        constexpr Colour(uint8_t r, uint8_t g, uint8_t b, uint8_t a): b{b}, g{g}, r{r}, a{a} {}

        constexpr Colour operator+(const Colour& b) const {
            return {(uint8_t)(r + b.r), (uint8_t)(g + b.g), (uint8_t)(this->b + b.b), a};
        }

        uint32_t raw;
        struct {
            uint8_t b, g, r, a;
        };
    };
    static_assert(sizeof(Colour) == 4);

    struct Desktop;

    struct Widget {
        virtual ~Widget() {}

        virtual void redraw(Desktop& desktop, const Vec2i& pos) = 0;
    };

    struct GuiEvent {
        enum class Type { MouseUpdate };
        Type type;
        Vec2i pos;
    };

    struct Desktop {
        Desktop(gpu::GpuManager& gpu) {
            fb = (volatile uint32_t*)gpu.get_fb();
            auto mode = gpu.get_mode();
            size = {mode.width, mode.height};
            pitch = mode.pitch / 4;
        }

        void add_window(Widget* widget) { widgets.push_back(widget); }

        void update() {
            gpu::get_gpu().clear_backbuffer();

            for(auto* widget : widgets)
                widget->redraw(*this, {0, 0});

            gpu::get_gpu().flush();
        }

        [[gnu::always_inline]]
        inline void put_pixel(const Vec2i& c, Colour colour) {
            if(colour.a > 0)
                fb[c.x + c.y * pitch] = colour.raw;
        }

        [[gnu::always_inline]]
        inline void put_char(const Vec2i& c, const char v, Colour fg, Colour bg) {
            constexpr uint8_t font_width = 8;
            constexpr uint8_t font_height = 16;

            auto* line = fb + c.y * pitch + c.x;

            auto dc = (v >= 32) ? v : 127;
            for(uint8_t i = 0; i < font_height; i++) {
                auto* dest = line;
                auto bits = font_bitmap[(dc - 32) * font_height + i];
                for(uint8_t j = 0; j < font_width; j++) {
                    auto bit = (1 << ((font_width - 1) - j));
                    *dest = (bits & bit) ? (fg.a == 255 ? fg.raw : *dest) : (bg.a == 255 ? bg.raw : *dest);
                    dest++;
                }
                line += pitch;
            }
        }

        EventQueue<GuiEvent>& get_event_queue() {
            return event_queue;
        }

        private:
        EventQueue<GuiEvent> event_queue;
        std::vector<Widget*> widgets;
        volatile uint32_t* fb;
        Vec2<size_t> size;
        size_t pitch;
    };

    void init();
    Desktop& get_desktop();
} // namespace gui
