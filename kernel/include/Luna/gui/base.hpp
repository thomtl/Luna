#pragma once

#include <Luna/common.hpp>
#include <Luna/misc/log.hpp>
#include <Luna/misc/font.hpp>

#include <std/concepts.hpp>
#include <std/vector.hpp>
#include <std/algorithm.hpp>
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

        Vec operator/(const T& b) const {
            return {x / b, y / b};
        }

        Vec& operator+=(const Vec& b) {
            x += b.x;
            y += b.y;

            return *this;
        }

        bool operator==(const Vec& b) {
            return (x == b.x) && (y == b.y);
        }

        void clamp(Vec min, Vec max) {
            x = ::clamp(x, min.x, max.x);
            y = ::clamp(y, min.y, max.y);
        }

        bool collides_with(Vec pos, Vec size) const {
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

        bool collides_with(const Vec2i& x) const {
            return (x.x >= pos.x && x.x <= (pos.x + size.x)) && (x.y >= pos.y && x.y <= (pos.y + size.y));
        }

        bool is_within(const Rect& super) const {
            return ((pos.x + size.x) < (super.pos.x + super.size.x)) && (pos.x > super.pos.x) && (pos.y > super.pos.y) && ((pos.y + size.y) < (super.pos.y + super.size.y));
        }
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

        constexpr Colour lerp(uint8_t t, const Colour& other) const {
            auto r = (this->r * (256 - t) + other.r * t) / 256;
            auto g = (this->g * (256 - t) + other.g * t) / 256;
            auto b = (this->g * (256 - t) + other.b * t) / 256;
            auto a = (this->a * (256 - t) + other.a * t) / 256;

            return {(uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a};
        }

        uint32_t raw;
        struct {
            uint8_t b, g, r, a;
        };
    };
    static_assert(sizeof(Colour) == 4);
    
    struct NonOwningCanvas {
        NonOwningCanvas() = default;
        NonOwningCanvas(std::span<Colour> fb, Vec2i size, std::optional<size_t> pitch = std::nullopt): size{size}, pitch{pitch.value_or(size.x)}, fb{fb} {
            ASSERT(fb.size() == (size_t)(size.y * this->pitch));
        }

        [[gnu::always_inline]]
        inline void put_pixel(const Vec2i& c, Colour colour) {
            if(colour.a > 0)
                fb[c.x + c.y * pitch] = colour.raw;
        }

        [[gnu::always_inline]]
        inline void blit(const Vec2i& dst_pos, const Vec2i& src_pos, const Vec2i& size, const NonOwningCanvas& bitmap) {
            Colour* dst_line = &fb[dst_pos.x + dst_pos.y * pitch];
            const Colour* src_line = &bitmap.fb[src_pos.x + src_pos.y * bitmap.pitch];
            
            for(int64_t i = 0; i < size.y; i++) {
                for(int64_t j = 0; j < size.x; j++) {
                    auto c = src_line[j];
                    if(c.a < 255)
                        continue;

                    dst_line[j] = c;
                }

                dst_line += pitch;
                src_line += bitmap.pitch;
            }
        }

        [[gnu::always_inline]]
        inline void blit_noalpha(const Vec2i& dst_pos, const Vec2i& src_pos, const Vec2i& size, const NonOwningCanvas& bitmap) {
            for(int64_t i = 0; i < size.y; i++) {
                auto dst_line_pos = dst_pos + Vec2i{0, i};
                auto src_line_pos = src_pos + Vec2i{0, i};
                memcpy((void*)(fb.data() + (dst_line_pos.x + dst_line_pos.y * pitch)), (void*)(bitmap.fb.data() + (src_line_pos.x + src_line_pos.y * bitmap.pitch)), size.x * sizeof(Colour));
            }
        }

        [[gnu::always_inline]]
        inline void put_char(const Vec2i& c, const char v, Colour fg, Colour bg) {
            auto y_cutoff = min((int64_t)font::width, size.x - c.x); // Partially draw the character if it is cutoff before the end of the scanline

            auto* line = fb.data() + c.y * pitch + c.x;

            auto dc = (v >= 32) ? v : 127;
            for(uint8_t i = 0; i < font::height; i++) {
                auto* dest = line;
                auto bits = font::bitmap[(dc - 32) * font::height + i];
                for(uint8_t j = 0; j < y_cutoff; j++) {
                    auto bit = (1 << ((font::width - 1) - j));
                    dest->raw = (bits & bit) ? (fg.a == 255 ? fg.raw : dest->raw) : (bg.a == 255 ? bg.raw : dest->raw);
                    dest++;
                }
                line += pitch;
            }
        }
        
        void clear(const Colour& clear_color = Colour(0, 0, 0)) {
            for(int64_t y = 0; y < size.y; y++)
                std::fill_n(fb.data() + y * pitch, size.x, clear_color);
        }

        NonOwningCanvas subcanvas(const Rect& rect) {
            return NonOwningCanvas{std::span<Colour>{fb.data() + rect.pos.x + rect.pos.y * pitch, rect.size.y * pitch}, rect.size, pitch};
        }

        Vec2i size;
        size_t pitch;
        std::span<Colour> fb;
    };

    struct Image {
        Image(): res{0, 0}, data{} {}
        
        Image(Vec2i res, Colour* bitmap = nullptr): res{res}, data{(size_t)(res.x * res.y)} {
            if(bitmap)
                memcpy(data.data(), bitmap, res.x * res.y * sizeof(Colour));
        }

        Image(Vec2i res, Colour fill_colour): res{res}, data{(size_t)(res.x * res.y), fill_colour} { }

        NonOwningCanvas canvas() { return NonOwningCanvas{data.span(), res}; }
        std::span<Colour> span() { return data.span(); }
        Vec2i get_res() const { return res; }

        Image crop(const Vec2i& new_res) {
            if(res == new_res)
                return *this;

            ASSERT(new_res.x < res.x && new_res.y < res.y);

            auto half_res = new_res / 2;
            auto src_center = res / 2;

            Rect src_rect{src_center - half_res, new_res};

            Image image{new_res};
            image.canvas().blit_noalpha({0, 0}, src_rect.pos, src_rect.size, canvas());

            return image;
        }



        private:
        Vec2i res;
        std::vector<Colour> data;
    };
} // namespace gui
