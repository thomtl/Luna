#pragma once

#include <Luna/common.hpp>
#include <Luna/gui/base.hpp>

#include <Luna/gui/framework.hpp>

#include <std/concepts.hpp>
#include <std/tuple.hpp>

namespace gui::controls {
    template<typename T>
    concept Control = requires(T t, const NonOwningCanvas& canvas) {
        { t.resize(canvas) } -> std::same_as<void>;
        { t.preferred_size() } -> std::convertible_to<Vec2i>;
    };

    struct Square {
        Square(Vec2i size, Colour c): size{size}, c{c} { }

        void resize(NonOwningCanvas canvas) {
            this->canvas = canvas;

            draw::rect(this->canvas, {0, 0}, canvas.size, c);
        }

        Vec2i preferred_size() const {
            return size;
        }

        private:
        Vec2i size;
        Colour c;

        NonOwningCanvas canvas;
    };
    static_assert(Control<Square>);

    enum class Direction { Vertical, Horizontal };
    struct Insets {
        int32_t left = 0, right = 0, top = 0, bottom = 0;
    };

    template<Direction direction, typename... Items> requires(Control<Items> && ...)
    struct Stack {
        Stack(const Insets& inset, Items&&... items): inset{inset}, tuple{std::forward<Items>(items)...} { }

        void resize(NonOwningCanvas canvas) {
            this->canvas = canvas;
            Rect usable_space{{inset.left, inset.top}, {canvas.size.x - inset.left - inset.right, canvas.size.y - inset.top - inset.bottom}};

            std::apply([&](auto&&... args) {
                Vec2i pos = usable_space.pos;

                auto f = [&](auto&& item) {
                    auto extent = item.preferred_size();
                    ASSERT((pos.x + extent.x) <= canvas.size.x && (pos.y + extent.y) <= canvas.size.y);

                    item.resize(canvas.subcanvas({pos, extent}));

                    if constexpr (direction == Direction::Vertical)
                        pos.y += extent.y;
                    else
                        pos.x += extent.x;
                };

                (f(args), ...);
            }, tuple);
        }

        Vec2i preferred_size() const {
            return std::apply([&](auto&&... args) {
                Vec2i ret = {0, 0};
                auto f = [&](auto&& item) {
                    auto size = item.preferred_size();

                    if constexpr(direction == Direction::Horizontal) {
                        ret.y = max(ret.y, size.y);
                        ret.x += size.x;
                    } else {
                        ret.x = max(ret.x, size.x);
                        ret.y += size.y;
                    }
                };

                (f(args), ...);

                ret.x += (inset.left + inset.right);
                ret.y += (inset.top + inset.bottom);

                return ret;
            }, tuple);
        }

        private:
        Insets inset;
        std::tuple<Items...> tuple;

        NonOwningCanvas canvas;
    };
    static_assert(Control<Stack<Direction::Horizontal>>);
    static_assert(Control<Stack<Direction::Vertical>>);
    static_assert(Control<Stack<Direction::Horizontal, Square, Square>>);

    template<typename... Args> using VStack = Stack<Direction::Vertical, Args...>;
    template<typename... Args> using HStack = Stack<Direction::Horizontal, Args...>;
} // namespace gui::controls