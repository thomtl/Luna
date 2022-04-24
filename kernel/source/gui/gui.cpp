#include <Luna/gui/gui.hpp>
#include <Luna/gui/framework.hpp>

using namespace gui;

#define W Colour{255, 255, 255}
#define G Colour{127, 127, 127}
#define B Colour{0, 0, 0, 0}

constexpr int cursor_size = 16;
constexpr Colour cursor[cursor_size * cursor_size] = {
    W, B, B, B, B, B, B, B, B, B, B, B, B, B, B, W,
    B, W, B, B, B, B, B, B, B, B, B, B, B, B, W, B,
    B, B, W, B, B, B, B, B, B, B, B, B, B, W, B, B,
    B, B, B, W, B, B, B, B, B, B, B, B, W, B, B, B,
    B, B, B, B, W, B, B, B, B, B, B, W, B, B, B, B,
    B, B, B, B, B, W, B, B, B, B, W, B, B, B, B, B,
    B, B, B, B, B, B, W, B, B, W, B, B, B, B, B, B,
    B, B, B, B, B, B, B, W, W, B, B, B, B, B, B, B,
    B, B, B, B, B, B, B, W, W, B, B, B, B, B, B, B,
    B, B, B, B, B, B, W, B, B, W, B, B, B, B, B, B,
    B, B, B, B, B, W, B, B, B, B, W, B, B, B, B, B,
    B, B, B, B, W, B, B, B, B, B, B, W, B, B, B, B,
    B, B, B, W, B, B, B, B, B, B, B, B, W, B, B, B,
    B, B, W, B, B, B, B, B, B, B, B, B, B, W, B, B,
    B, W, B, B, B, B, B, B, B, B, B, B, B, B, W, B,
    W, B, B, B, B, B, B, B, B, B, B, B, B, B, B, W,
};

#undef W
#undef G
#undef B

Desktop::Desktop(gpu::GpuManager& gpu): gpu{&gpu}, fb_canvas{Vec2i{(int32_t)gpu.get_mode().width, (int32_t)gpu.get_mode().height}} {
    fb = (volatile uint32_t*)gpu.get_fb();
    auto mode = gpu.get_mode();
    size = {mode.width, mode.height};
    pitch = mode.pitch / 4;

    mouse.pos = {(int)size.x / 2, (int)size.y / 2};
    mouse.is_dragging = false;

    spawn([this] {
        constexpr int32_t decoration_side_width = 3;
        constexpr int32_t decoration_top_width = 18;
        constexpr auto decoration_colour = Colour(200, 200, 200);

        while(true) {
            event_queue.handle([this](auto& event) {
                if(event.type == GuiEvent::Type::MouseUpdate) {
                    mouse.pos += event.pos;
                    mouse.pos.clamp({0, 0}, {(int)size.x - cursor_size, (int)size.y - cursor_size});
                    mouse.left_button_down = event.left_button_down;
                } else {
                    print("gui: Unknown Event Type: {}\n", (uint32_t)event.type);
                }
            });
            
            if(mouse.is_dragging) {
                if(!mouse.left_button_down) {
                    mouse.is_dragging = false;
                } else {
                    mouse.dragging_window->pos = mouse.pos + mouse.dragging_offset;
                    mouse.dragging_window->pos.clamp(Vec2i{decoration_side_width, decoration_top_width + 20}, Vec2i{(int)size.x, (int)size.y} - mouse.dragging_window->window->canvas.size - Vec2i{decoration_side_width, decoration_top_width});
                }
            }

            fb_canvas.clear();

            for(auto& window : windows) {
                // Draw decorations
                auto size = window.window->canvas.size;

                using namespace draw;

                rect(fb_canvas, window.pos - Vec2i{decoration_side_width, 0}, Vec2i{decoration_side_width, size.y}, decoration_colour); // Left bar
                rect(fb_canvas, window.pos + Vec2i{size.x, 0}, Vec2i{decoration_side_width, size.y}, decoration_colour); // Right bar

                rect(fb_canvas, window.pos + Vec2i{-decoration_side_width, size.y}, Vec2i{size.x + 2 * decoration_side_width, decoration_side_width}, decoration_colour); // Bottom bar

                Rect top_bar{window.pos - Vec2i{decoration_side_width, decoration_top_width}, Vec2i{size.x + 2 * decoration_side_width, decoration_top_width}};
                rect(fb_canvas, top_bar, decoration_colour); // Top bar
                text(fb_canvas, window.pos - Vec2i{decoration_side_width, decoration_top_width - 1} + Vec2i{size.x / 2, 0}, window.window->title, Colour{0, 0, 0}, decoration_colour, TextAlign::Center);

                if(mouse.pos.collides_with(top_bar.pos, top_bar.size) && mouse.left_button_down) {
                    mouse.dragging_window = &window;
                    mouse.dragging_offset = window.pos - mouse.pos;
                    mouse.is_dragging = true;
                }
            
                // Actually blit screen
                fb_canvas.blit(window.pos, window.window->canvas.size, window.window->canvas.fb.data());
            }

            // Draw mouse
            fb_canvas.blit(mouse.pos, Vec2i{cursor_size, cursor_size}, cursor);

            // Draw topbar
            draw::rect(fb_canvas, Vec2i{0, 0}, Vec2i{(int)size.x, 20}, Colour{203, 45, 62});
            draw::text(fb_canvas, Vec2i{2, 2}, "Luna", Colour{255, 214, 191}, Colour{0, 0, 0, 0});

            memcpy(this->gpu->get_main_gpu()->get_lfb(), fb_canvas.fb.data(), fb_canvas.fb.size() * 4);
        }
    });
}


static Desktop* desktop;

void gui::init() {
    auto& gpu = gpu::get_gpu();

    desktop = new Desktop{gpu};
}

Desktop& gui::get_desktop() {
    ASSERT(desktop);
    return *desktop;
}