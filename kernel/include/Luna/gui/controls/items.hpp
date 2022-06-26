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

    struct Button final : public Control {
        Button(const char* text, Vec2i size, Colour c): text{text}, size{size}, c{c}, state{false} {
            ASSERT(size.y >= font::height && (uint32_t)size.x >= (strlen(text) * font::width));
        }

        void resize(NonOwningCanvas canvas) override {
            this->canvas = canvas;

            ASSERT(canvas.size.x >= size.x && canvas.size.y >= size.y);

            draw(c);
            state = false;
        }

        Vec2i preferred_size() const override {
            return size;
        }

        void mouse_over(const Vec2i&) override {
            if(!state)
                draw({128, 128, 128}); 
            
            state = true;
        };
        void mouse_exit() override { if(state) draw(c); state = false; }

        private:
        void draw(const Colour& c) {
            draw::rect(canvas, {0, 0}, canvas.size, c);
            draw::text(canvas, {canvas.size.x / 2, canvas.size.y / 2 - font::height / 2}, text, {255, 255, 255}, {0, 0, 0, 0}, draw::TextAlign::Center);
        }

        const char* text;
        Vec2i size;
        Colour c;

        bool state;

        NonOwningCanvas canvas;
    };
} // namespace gui::controls