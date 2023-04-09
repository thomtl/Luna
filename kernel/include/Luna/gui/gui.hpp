#pragma once

#include <Luna/common.hpp>
#include <Luna/drivers/gpu/gpu.hpp>
#include <Luna/cpu/threads.hpp>

#include <Luna/gui/base.hpp>
#include <Luna/gui/controls/base.hpp>

#include <std/event_queue.hpp>

namespace gui {
    struct RawWindow;

    enum class KeyOp { Press, Repeat, Unpress };
    enum class KeyCodes : uint32_t {
        Unknown = 0,
        A, B, C, D, E, F, G, H, I, J, K, L, M, 
        N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        _0, _1, _2, _3, _4, _5, _6, _7, _8, _9,
        Enter, Tab, Escape, Delete, Space, BackSpace,
        Minus, Plus, Star, Equals, 
        BraceOpen, BraceClose, ParenOpen, ParenClose, BackSlash, Tilde, Semicolon, Comma, Dot, Slash,
        F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
        F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24,
        CapsLock, NumLock, ScrollLock, PrntScrn, Pause, Insert, Home, PageUp,
        End, PageDown, RightArrow, LeftArrow, DownArrow, UpArrow,
        LeftShift, RightShift, LeftControl, RightControl, LeftAlt, RightAlt, LeftGUI, RightGUI,
    };
    char keycode_to_char(const KeyCodes& c);

    struct CompositorEvent {
        enum class Type { MouseUpdate, KeyboardUpdate, WindowRedraw };
        Type type;
        union {
            struct {
                Vec2i pos = {0, 0};
                bool left_button_down;
            } mouse;

            struct {
                KeyOp op;
                KeyCodes key;
            } keyboard;

            struct {
                RawWindow* window;
            } window;
        };
    };

    struct WindowEvent {
        enum class Type { Focus, Unfocus, MouseOver, MouseExit, MouseClick, KeyboardOp };
        Type type;
        union {
            struct {
                Vec2i pos = {0, 0};
            } mouse;

            struct {
                KeyOp op;
                KeyCodes code;
            } keyboard;

            struct {

            } none;
        };
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
        virtual void handle_keyboard_op(KeyOp, KeyCodes) { }
        

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
                            handle_mouse_over(event.mouse.pos);
                            break;
                        case MouseExit:
                            handle_mouse_exit();
                            break;
                        case MouseClick:
                            handle_mouse_click();
                            break;
                        case KeyboardOp:
                            handle_keyboard_op(event.keyboard.op, event.keyboard.code);
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
        virtual void handle_mouse_exit() override { root->mouse_exit(); }
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
            window->set_pos({4 + size, 40});
            window->set_queue(&get_event_queue());
            
            size += rect.size.x + 7;

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
        bool mouse_has_left_window;

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
    std::optional<Image> read_image(const char* path);
} // namespace gui
