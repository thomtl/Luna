#pragma once

#include <Luna/common.hpp>
#include <Luna/drivers/gpu/gpu.hpp>
#include <Luna/cpu/threads.hpp>

#include <Luna/gui/base.hpp>

#include <std/event_queue.hpp>

namespace gui {
    struct Window;

    struct GuiEvent {
        enum class Type { MouseUpdate, WindowRedraw };
        Type type;
        union {
            struct {
                Vec2i pos;
                bool left_button_down;
            } mouse;

            struct {
                Window* window;
            } window;
        };
    };

    struct Window {
        Window(Vec2i size, const char* title): fb{(size_t)(size.x * size.y)}, canvas{fb.span(), size}, title{title}, pos{0, 0}, size{size} {}
        virtual ~Window() {}

        Rect get_rect() const { return Rect{pos, size}; }
        const char* get_title() const { return title; }
        const NonOwningCanvas& get_canvas() const { return canvas; }

        void set_pos(const Vec2i& pos) { this->pos = pos; }
        void set_queue(std::EventQueue<GuiEvent>* queue) { this->queue = queue; }

        void request_redraw() {
            ASSERT(queue);
            queue->push(GuiEvent{.type = GuiEvent::Type::WindowRedraw, .window = {.window = this}});
        }

        protected:
        std::vector<Colour> fb;
        NonOwningCanvas canvas;
        const char* title;

        Vec2i pos;
        Vec2i size;

        private:
        std::EventQueue<GuiEvent>* queue;
    };

    struct Desktop {
        Desktop(gpu::GpuManager& gpu);
        void add_window(Window* window) {
            static int size = 0;

            auto rect = window->get_rect();
            window->set_pos({5 + size, 40});
            window->set_queue(&get_event_queue());
            
            size += rect.size.x + 8;

            std::lock_guard guard{compositor_lock};
            windows.push_back(window);
        }

        std::EventQueue<GuiEvent>& get_event_queue() { return event_queue; }

        private:
        void redraw_desktop();

        gpu::GpuManager* gpu;
        gpu::Mode gpu_mode;

        std::EventQueue<GuiEvent> event_queue;
        NonOwningCanvas fb_canvas;

        IrqTicketLock compositor_lock;
        std::vector<Window*> windows;

        //volatile uint32_t* fb;
        Vec2i size;
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
