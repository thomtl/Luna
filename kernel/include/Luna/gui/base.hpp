#pragma once

#include <Luna/common.hpp>
#include <Luna/misc/log.hpp>
#include <Luna/misc/font.hpp>

#include <std/concepts.hpp>
#include <std/vector.hpp>
#include <std/optional.hpp>

namespace gui {
    template<typename T, size_t N> requires std::integral<T> || std::floating_point<T>
    struct Vec;
    
    template<typename T>
    struct Vec<T, 2> {
        union {
            struct { T x, y; };
            struct { T r, g; };
            struct { T s, t; };
        };

        Vec operator+(const Vec& b) const {
            return {x + b.x, y + b.y};
        }

        Vec operator-(const Vec& b) const {
            return {x - b.x, y - b.y};
        }

        Vec& operator+=(const Vec& b) {
            x += b.x;
            y += b.y;

            return *this;
        }

        void clamp(Vec min, Vec max) {
            x = ::clamp(x, min.x, max.x);
            y = ::clamp(y, min.y, max.y);
        }

        void lerp(Vec b, float a) {
            x = x * (1 - a) + b.x * a;
            y = y * (1 - a) + b.y * a;
        }

        bool collides_with(Vec pos, Vec size) {
            return (x >= pos.x && x <= (pos.x + size.x)) && (y >= pos.y && y <= (pos.y + size.y));
        }
    };
    
    template<typename T>
    using Vec2 = Vec<T, 2>;

    using Vec2i = Vec2<int64_t>;
    using Vec2u = Vec2<uint64_t>;

    struct Rect {
        Vec2i pos;
        Vec2i size;
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
    
    struct NonOwningCanvas {
        NonOwningCanvas(std::span<Colour> fb, Vec2i size, std::optional<size_t> pitch = std::nullopt): size{size}, pitch{pitch.value_or(size.x)}, fb{fb} {
            ASSERT(fb.size() == (size_t)(size.y * this->pitch));

            clear();
        }

        [[gnu::always_inline]]
        inline void put_pixel(const Vec2i& c, Colour colour) {
            if(colour.a > 0)
                fb[c.x + c.y * pitch] = colour.raw;
        }

        [[gnu::always_inline]]
        inline void blit(const Vec2i& pos, const Vec2i& size, const Colour* bitmap) {
            Colour* line = &fb[pos.x + pos.y * pitch];
            
            for(int64_t i = 0; i < size.y; i++) {
                //auto line_pos = pos + Vec2i{0, i};
                //memcpy((void*)(fb + (line_pos.x + line_pos.y * pitch)), bitmap + i * size.y, size.x * sizeof(Colour));

                for(int64_t j = 0; j < size.x; j++) {
                    auto c = bitmap[i * size.x + j];
                    if(c.a < 255)
                        continue;
                    line[j] = c;
                }

                line += pitch;
            }
        }

        [[gnu::always_inline]]
        inline void blit_noalpha(const Vec2i& pos, const Vec2i& size, const Colour* bitmap) {
            for(int64_t i = 0; i < size.y; i++) {
                auto line_pos = pos + Vec2i{0, i};
                memcpy((void*)(fb.data() + (line_pos.x + line_pos.y * pitch)), bitmap + i * size.x, size.x * sizeof(Colour));
            }
        }

        [[gnu::always_inline]]
        inline void put_char(const Vec2i& c, const char v, Colour fg, Colour bg) {
            auto* line = fb.data() + c.y * pitch + c.x;

            auto dc = (v >= 32) ? v : 127;
            for(uint8_t i = 0; i < font::height; i++) {
                auto* dest = line;
                auto bits = font::bitmap[(dc - 32) * font::height + i];
                for(uint8_t j = 0; j < font::width; j++) {
                    auto bit = (1 << ((font::width - 1) - j));
                    dest->raw = (bits & bit) ? (fg.a == 255 ? fg.raw : dest->raw) : (bg.a == 255 ? bg.raw : dest->raw);
                    dest++;
                }
                line += pitch;
            }
        }
        
        void clear() {
            for(auto& e : fb)
                e = Colour{0, 0, 0};
        }

        Vec2i size;
        size_t pitch;
        std::span<Colour> fb;
    };
} // namespace gui
