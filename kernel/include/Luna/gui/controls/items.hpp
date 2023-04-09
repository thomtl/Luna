#pragma once

#include <Luna/common.hpp>
#include <Luna/gui/controls/base.hpp>
#include <Luna/misc/debug.hpp>

namespace gui::controls {   
    struct Square final : public Control {
        constexpr Square() = default;
        constexpr Square(Vec2i size, Colour c): size{size}, c{c} { }

        void resize(NonOwningCanvas canvas) override {
            this->canvas = canvas;

            draw::rect(this->canvas, {0, 0}, canvas.size, c);
        }

        Vec2i preferred_size() const override {
            return size;
        }

        void mouse_over(const Vec2i&) override {
            draw::rect(this->canvas, {0, 0}, canvas.size, Colour{255, 255, 255});
        }

        void mouse_exit() override {
            draw::rect(this->canvas, {0, 0}, canvas.size, c);
        }

        private:
        Vec2i size;
        Colour c;

        NonOwningCanvas canvas;
    };

    struct Text final : public Control {
        Text(const char* text): text{text} { }

        void resize(NonOwningCanvas canvas) override {
            this->canvas = canvas;

            auto extent = preferred_size();
            ASSERT(this->canvas.size.y >= extent.y && this->canvas.size.x >= extent.x);

            draw::text(this->canvas, {0, 0}, text);        
        }

        Vec2i preferred_size() const override {
            return {(int32_t)strlen(text) * font::width, font::height};
        }

        private:
        const char* text;

        NonOwningCanvas canvas;
    };
        
    struct ImageButton final : public Control {
        ImageButton() = default;
        ImageButton(const Image& unclicked, const Image& hover, void (*f)(void*), void* userptr): fn{f}, userptr{userptr}, state{State::Undrawn} {
            this->unclicked_glyph = unclicked;
            this->hover_glyph = hover;

            ASSERT(this->unclicked_glyph.get_res() == this->hover_glyph.get_res());
            this->res = this->unclicked_glyph.get_res();
        }

        void resize(NonOwningCanvas canvas) override {
            this->canvas = canvas;

            ASSERT(canvas.size == res);

            update(State::Unclicked);
        }

        Vec2i preferred_size() const override {
            return res;
        }

        void mouse_over(const Vec2i&) override {
            update(State::Hover);
        }

        void mouse_exit() override {
            update(State::Unclicked);
        }

        void mouse_click() override {
            fn(userptr);
        }

        private:
        enum class State { Undrawn, Hover, Unclicked };

        void update(State new_state) {
            if(new_state != state) {
                state = new_state;

                Image& glyph = (state == State::Hover) ? hover_glyph : unclicked_glyph;
                canvas.blit_noalpha({0, 0}, {0, 0}, glyph.get_res(), glyph.canvas());
            }
        }

        Image unclicked_glyph, hover_glyph;
        Vec2i res;

        void (*fn)(void*);
        void* userptr;

        State state;

        NonOwningCanvas canvas;
    };

    struct TextButton final : public Control {
        TextButton() = default;
        TextButton(Vec2i size, const char* text, Colour bg_unclicked, Colour bg_hover, Colour fg_unclicked, Colour fg_hover, void (*f)(void*), void* userptr): bg_unclicked{bg_unclicked}, bg_hover{bg_hover}, fg_unclicked{fg_unclicked}, fg_hover{fg_hover}, size{size}, text{text}, fn{f}, userptr{userptr}, state{State::Undrawn} {
            text_offset = {(int32_t)(size.x - (strlen(text) * font::width)) / 2, (size.y - font::height) / 2};
        }

        void resize(NonOwningCanvas canvas) override {
            this->canvas = canvas;

            ASSERT(canvas.size == size);

            update(State::Unclicked);
        }

        Vec2i preferred_size() const override { return size; }

        void mouse_over(const Vec2i&) override {
            static int i = 0;
            print("Over{}\n", i++);
            update(State::Hover);
        }

        void mouse_exit() override {
            static int i = 0;
            print("Under{}\n",i++);
            update(State::Unclicked);
        }

        void mouse_click() override {
            fn(userptr);
        }

        private:
        enum class State { Undrawn, Hover, Unclicked };

        void update(State new_state) {
            if(new_state != state) {
                state = new_state;

                Colour bg = (state == State::Hover) ? bg_hover : bg_unclicked;
                Colour fg = (state == State::Hover) ? fg_hover : fg_unclicked;

                draw::rect(canvas, {{0, 0}, size}, bg);
                draw::text(canvas, text_offset, text, fg, bg);
            }
        }

        Colour bg_unclicked, bg_hover;
        Colour fg_unclicked, fg_hover;
        Vec2i text_offset, size;

        const char* text;

        void (*fn)(void*);
        void* userptr;

        State state;

        NonOwningCanvas canvas;
    };

    struct ScrollBar final : public Control {
        constexpr ScrollBar() = default;

        ScrollBar(const Vec2i& size, Colour fg = Colour{255, 255, 255}, Colour bg = Colour{0, 0, 0}): size{size}, fg{fg}, bg{bg} { }

        Vec2i preferred_size() const override { return size; }
        void resize(NonOwningCanvas canvas) override {
            this->canvas = canvas;
            ASSERT(canvas.size == size);
            ASSERT(canvas.fb.data());
        }

        void update(size_t total_size, size_t visible_region, size_t pos) {
            int64_t bar_size = (size.y * visible_region) / total_size;
            bar_size = clamp(bar_size, 0, size.y);

            int64_t bar_pos = (size.y * pos) / total_size;
            bar_pos = clamp(bar_pos, 0, size.y);
            
            canvas.clear(bg);
            draw::rect(canvas, Vec2i{0, (int64_t)bar_pos}, Vec2i{size.x, bar_size}, fg);
        }

        private:
        NonOwningCanvas canvas;

        Vec2i size;
        Colour fg, bg;
    };

    /*struct Space final : public Control {
        constexpr Space() = default;

        Space(const Vec2i& space): space{space} { }

        void resize(NonOwningCanvas) override { }

        Vec2i preferred_size() const override {
            return space;
        }

        private:
        Vec2i space;
    };*/
} // namespace gui::controls