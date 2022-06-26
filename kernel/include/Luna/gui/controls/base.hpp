#pragma once

#include <Luna/common.hpp>
#include <Luna/gui/base.hpp>

#include <Luna/gui/framework.hpp>

#include <std/array.hpp>
#include <std/concepts.hpp>
#include <std/tuple.hpp>

namespace gui::controls {
    struct Control {
        virtual ~Control() = default;

        virtual void resize(NonOwningCanvas) = 0;
        virtual Vec2i preferred_size() const = 0;
        virtual void mouse_over(const Vec2i&) { };
        virtual void mouse_exit() { };
    };

    enum class Direction { Vertical, Horizontal };
    struct Insets {
        int32_t left = 0, right = 0, top = 0, bottom = 0;
    };

    template<Direction direction, typename... Items> requires(std::derived_from<Items, Control> && ...)
    struct Stack final : public Control {
        Stack(const Insets& inset, Items... items): inset{inset}, last_mouse_over{nullptr}, storage{items...}, items{} {
            std::comptime_iterate_to<sizeof...(Items)>([&]<size_t I>() { std::get<I>(this->items) = {&std::get<I>(storage), Rect{}}; });
        }

        Stack(Items... items): Stack{Insets{}, items...} { }

        Stack(const Stack& other) {
            inset = other.inset;
            canvas = other.canvas;
            last_mouse_over = other.last_mouse_over;
            last_mouse_extent = other.last_mouse_extent;

            storage = other.storage;
            for(size_t i = 0; i < items.size(); i++)
                items[i] = {nullptr, other.items[i].second};

            std::comptime_iterate_to<sizeof...(Items)>([&]<size_t I>() { std::get<I>(this->items).first = &std::get<I>(storage); });
        }

        Stack(Stack&&) = delete;
        Stack& operator=(const Stack&) = delete;
        Stack& operator=(Stack&&) = delete;

        void resize(NonOwningCanvas canvas) override {
            this->canvas = canvas;
            Rect usable_space{{inset.left, inset.top}, {canvas.size.x - inset.left - inset.right, canvas.size.y - inset.top - inset.bottom}};

            Vec2i pos = usable_space.pos;
            for(auto& [item, extent] : items) {
                auto preferred_extent = item->preferred_size();
                ASSERT((pos.x + preferred_extent.x) <= canvas.size.x && (pos.y + preferred_extent.y) <= canvas.size.y);

                Rect subrect{pos, preferred_extent};
                extent = subrect;

                item->resize(canvas.subcanvas(subrect));

                if constexpr (direction == Direction::Vertical)
                    pos.y += extent.size.y;
                else
                    pos.x += extent.size.x;
            }
        }

        void mouse_over(const Vec2i& pos) override {
            if(last_mouse_over && !last_mouse_extent.collides_with(pos)) {
                last_mouse_over->mouse_exit();
                last_mouse_over = nullptr;
            }
            
            for(auto& [item, extent] : items) {
                if(extent.collides_with(pos)) {
                    item->mouse_over(pos - extent.pos);

                    if(last_mouse_over && last_mouse_over != item) {
                        last_mouse_over->mouse_exit();
                    }

                    last_mouse_over = item;
                    break;
                }
            }
        }

        void mouse_exit() override {
            if(last_mouse_over)
                last_mouse_over->mouse_exit();
        }


        Vec2i preferred_size() const override {
            Vec2i ret = {0, 0};

            for(auto& [item, _] : items) {
                auto size = item->preferred_size();

                if constexpr(direction == Direction::Horizontal) {
                    ret.y = max(ret.y, size.y);
                    ret.x += size.x;
                } else {
                    ret.x = max(ret.x, size.x);
                    ret.y += size.y;
                }
            }

            ret.x += (inset.left + inset.right);
            ret.y += (inset.top + inset.bottom);

            return ret;
        }

        private:
        Insets inset;
        NonOwningCanvas canvas;

        Control* last_mouse_over;
        Rect last_mouse_extent;

        std::tuple<Items...> storage;
        std::array<std::pair<Control*, Rect>, sizeof...(Items)> items;
    };

    template<typename... Args> using VStack = Stack<Direction::Vertical, Args...>;
    template<typename... Args> using HStack = Stack<Direction::Horizontal, Args...>;
} // namespace gui::controls