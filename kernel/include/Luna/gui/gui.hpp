#pragma once

#include <Luna/common.hpp>
#include <Luna/drivers/gpu/gpu.hpp>
#include <Luna/cpu/threads.hpp>
#include <Luna/misc/log.hpp>

#include <std/event_queue.hpp>

extern uint8_t font_bitmap[];

namespace gui {
    template<typename T>
    struct Vec2 {
        T x, y;

        Vec2 operator+(const Vec2& b) const {
            return {x + b.x, y + b.y};
        }

        Vec2 operator-(const Vec2& b) const {
            return {x - b.x, y - b.y};
        }

        Vec2& operator+=(const Vec2& b) {
            x += b.x;
            y += b.y;

            return *this;
        }

        void clamp(Vec2 min, Vec2 max) {
            x = ::clamp(x, min.x, max.x);
            y = ::clamp(y, min.y, max.y);
        }

        void lerp(Vec2 b, float a) {
            x = x * (1 - a) + b.x * a;
            y = y * (1 - a) + b.y * a;
        }

        bool collides_with(Vec2 pos, Vec2 size) {
            return (x >= pos.x && x <= (pos.x + size.x)) && (y >= pos.y && y <= (pos.y + size.y));
        }
    };
    using Vec2i = Vec2<int32_t>;

    struct Rect {
        Vec2i pos, size;
    };

    union Colour {
        constexpr Colour(): Colour{0, 0, 0} {}
        constexpr Colour(uint32_t v): raw{v} {}
        constexpr Colour(uint32_t v, uint8_t a): raw{v | (a << 24)} {}
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

    struct Canvas {
        Canvas(Vec2i size): size{size} {
            fb.resize(size.x * size.y);
        }

        [[gnu::always_inline]]
        inline void put_pixel(const Vec2i& c, Colour colour) {
            if(colour.a > 0)
                fb[c.x + c.y * size.x] = colour.raw;
        }

        [[gnu::always_inline]]
        inline void blit(const Vec2i& pos, const Vec2i& size, const Colour* bitmap) {
            Colour* line = &fb[pos.x + pos.y * this->size.x];
            
            for(int32_t i = 0; i < size.y; i++) {
                //auto line_pos = pos + Vec2i{0, i};
                //memcpy((void*)(fb + (line_pos.x + line_pos.y * pitch)), bitmap + i * size.y, size.x * sizeof(Colour));

                for(int32_t j = 0; j < size.x; j++) {
                    auto c = bitmap[i * size.x + j];
                    if(c.a < 255)
                        continue;
                    line[j] = c;
                }

                line += this->size.x;
            }
        }

        [[gnu::always_inline]]
        inline void put_char(const Vec2i& c, const char v, Colour fg, Colour bg) {
            constexpr uint8_t font_width = 8;
            constexpr uint8_t font_height = 16;

            auto* line = fb.data() + c.y * size.x + c.x;

            auto dc = (v >= 32) ? v : 127;
            for(uint8_t i = 0; i < font_height; i++) {
                auto* dest = line;
                auto bits = font_bitmap[(dc - 32) * font_height + i];
                for(uint8_t j = 0; j < font_width; j++) {
                    auto bit = (1 << ((font_width - 1) - j));
                    dest->raw = (bits & bit) ? (fg.a == 255 ? fg.raw : dest->raw) : (bg.a == 255 ? bg.raw : dest->raw);
                    dest++;
                }
                line += size.x;
            }
        }
        
        void clear() {
            //memset(fb.data(), 0, fb.size() * sizeof(Colour));
            for(auto& item : fb)
                item = Colour{0, 0, 0}; // Make sure alpha is set
        }

        Vec2i size;
        std::vector<Colour> fb;
    };

    struct Window {
        Window(Vec2i size, const char* title): canvas{size}, title{title}, pos{0, 0}, size{size} {}
        virtual ~Window() {}

        const Canvas& get_rendering_info() { return canvas; }

        mutable Canvas canvas;
        const char* title;

        Vec2i pos, size;
    };

    struct GuiEvent {
        enum class Type { MouseUpdate };
        Type type;
        Vec2i pos;
        bool left_button_down;
    };

    struct Desktop {
        Desktop(gpu::GpuManager& gpu);
        void add_window(Window* window) {
            static int size = 0;

            window->pos = {5 + size, 40};
            
            size += window->canvas.size.x + 8;

            std::lock_guard guard{compositor_lock};
            windows.push_back(window);
        }

        std::EventQueue<GuiEvent>& get_event_queue() { return event_queue; }

        private:
        gpu::GpuManager* gpu;
        std::EventQueue<GuiEvent> event_queue;
        Canvas fb_canvas;

        IrqTicketLock compositor_lock;
        std::vector<Window*> windows;

        volatile uint32_t* fb;
        Vec2<size_t> size;
        size_t pitch;

        struct {
            Vec2i pos;

            bool left_button_down;
            
            bool is_dragging;
            Window* dragging_window;
            Vec2i dragging_offset;
        } mouse;
    };

    void init();
    Desktop& get_desktop();
} // namespace gui
