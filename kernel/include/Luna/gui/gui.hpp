#pragma once

#include <Luna/common.hpp>
#include <Luna/drivers/gpu/gpu.hpp>
#include <Luna/cpu/threads.hpp>

#include <Luna/gui/base.hpp>
#include <Luna/gui/controls/base.hpp>

#include <std/event_queue.hpp>

namespace gui {
    struct RawWindow;

    struct CompositorEvent {
        enum class Type { MouseUpdate, WindowRedraw };
        Type type;
        union {
            struct {
                Vec2i pos;
                bool left_button_down;
            } mouse;

            struct {
                RawWindow* window;
            } window;
        };
    };

    struct WindowEvent {
        enum class Type { Focus, Unfocus, MouseOver, MouseClick };
        Type type;
        Vec2i pos = {0, 0};
    };

    struct RawWindow {
        RawWindow(Vec2i size, const char* title): fb{size}, canvas{fb.canvas()}, title{title}, pos{0, 0}, size{size} {
            spawn([this] { window_thread(); } );
        }
        virtual ~RawWindow() {}

        RawWindow(const RawWindow&) = delete;
        RawWindow(RawWindow&&) = delete;
        RawWindow& operator=(const RawWindow&) = delete;
        RawWindow& operator=(RawWindow&&) = delete;

        // Handlers
        virtual void handle_focus() { }
        virtual void handle_unfocus() { }
        virtual void handle_mouse_over(const Vec2i&) { }
        virtual void handle_mouse_click() { }
        virtual void handle_mouse_exit() { }
        

        // Public API
        Rect get_rect() const { return Rect{pos, size}; }
        const char* get_title() const { return title; }
        const NonOwningCanvas& get_canvas() const { return canvas; }

        void set_pos(const Vec2i& pos) { this->pos = pos; }
        void set_queue(std::EventQueue<CompositorEvent>* queue) { this->compositor_queue = queue; }

        void request_redraw() {
            ASSERT(compositor_queue);
            compositor_queue->push(CompositorEvent{.type = CompositorEvent::Type::WindowRedraw, .window = {.window = this}});
        }

        void send_event(const WindowEvent& event) {
            event_queue.push(event);
        }

        protected:
        controls::Control* root;
        Image fb;
        NonOwningCanvas canvas;
        const char* title;

        Vec2i pos;
        Vec2i size;

        std::EventQueue<WindowEvent> event_queue;

        private:
        void window_thread() {
            while(1) {
                event_queue.handle_await([&](const WindowEvent& event) {
                    using enum WindowEvent::Type;
                    switch(event.type) {
                        case Focus:
                            handle_focus();
                            break;
                        case Unfocus:
                            handle_unfocus();
                            break;
                        case MouseOver:
                            handle_mouse_over(event.pos);
                            break;
                        case MouseClick:
                            handle_mouse_click();
                            break;
                    }
                });
            }
        }

        std::EventQueue<CompositorEvent>* compositor_queue;
    };

    template<typename T = controls::Control> requires controls::IsControl<T>
    struct Window : public RawWindow {
        Window(T* root, const char* title): RawWindow{root->preferred_size(), title}, root{root} { }
        virtual ~Window() { }

        virtual void handle_unfocus() override { root->mouse_exit(); }
        virtual void handle_mouse_over(const Vec2i& pos) override { root->mouse_over(pos); }
        virtual void handle_mouse_click() override { root->mouse_click(); }

        protected:
        T* root;
    };

    struct Desktop {
        Desktop(gpu::GpuManager& gpu);
        void start_gui();
        
        void add_window(RawWindow* window) {
            static int size = 0;

            auto rect = window->get_rect();
            window->set_pos({5 + size, 40});
            window->set_queue(&get_event_queue());
            
            size += rect.size.x + 8;

            std::lock_guard guard{compositor_lock};
            windows.push_back(window);
        }

        std::EventQueue<CompositorEvent>& get_event_queue() { return event_queue; }

        private:
        void redraw_desktop(bool mouse_click);

        gpu::GpuManager* gpu;
        gpu::Mode gpu_mode;

        std::EventQueue<CompositorEvent> event_queue;
        NonOwningCanvas fb_canvas;

        IrqTicketLock compositor_lock;
        std::vector<RawWindow*> windows;
        RawWindow* focused_window;

        Image background, cursor;

        //volatile uint32_t* fb;
        Vec2i size;
        size_t pitch;

        struct {
            Vec2i pos;

            bool left_button_down;
            
            bool is_dragging;
            RawWindow* dragging_window;
            Vec2i dragging_offset;
        } mouse;
    };

    void init();
    Desktop& get_desktop();
} // namespace gui
