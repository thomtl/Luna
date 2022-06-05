#include <Luna/gui/gui.hpp>
#include <Luna/gui/framework.hpp>

#include <Luna/gui/bmp_parser.hpp>
#include <Luna/fs/vfs.hpp>

using namespace gui;

Desktop::Desktop(gpu::GpuManager& gpu): gpu{&gpu}, gpu_mode{gpu.get_mode()}, fb_canvas{std::span<Colour>{(Colour*)gpu.get_fb().data(), (gpu_mode.height * gpu_mode.pitch) / sizeof(Colour)}, Vec2i{(int64_t)gpu_mode.width, (int64_t)gpu_mode.height}, gpu_mode.pitch/ sizeof(Colour)} {
    size = {(int64_t)gpu_mode.width, (int64_t)gpu_mode.height};
    pitch = gpu_mode.pitch / 4;

    mouse.pos = {(int64_t)size.x / 2, (int64_t)size.y / 2};
    mouse.is_dragging = false;

}

void Desktop::start_gui() {
    {
        auto* file = vfs::get_vfs().open("A:/luna/assets/background.bmp");
        if(file) {
            auto image = bmp_parser::parse_bmp(file);
            if(!image)
                PANIC("Couldn't load background");

            file->close();

            background = image->crop(size);
        } else {
            background = Image{size, {255, 0, 0}};
            auto fb = background.span();

            constexpr Colour gradient_a{104, 242, 205};
            constexpr Colour gradient_b{121, 39, 234};
            for(int64_t y = 0; y < size.y; y++) {
                uint8_t idx = y / (size.y / 256);
                auto c = gradient_a.lerp(idx, gradient_b);

                std::fill_n(fb.begin() + y * size.x, size.x, c);
            }
        }
    }

    {
        auto file = vfs::get_vfs().open("A:/luna/assets/cursor.bmp");
        ASSERT(file);
        auto image = bmp_parser::parse_bmp(file);
        if(!image)
            PANIC("Couldn't load cursor");
        file->close();

        cursor = std::move(*image);
    }

    spawn([this] {
        while(true) {
            bool redraw = false;

            event_queue.handle_await([this, &redraw](GuiEvent& event) {
                switch (event.type) {
                    using enum GuiEvent::Type;
                    case MouseUpdate:
                        mouse.pos += event.mouse.pos;
                        mouse.pos.clamp({0, 0}, size - cursor.get_res());
                        mouse.left_button_down = event.mouse.left_button_down;

                        redraw = true;
                        break;

                    case WindowRedraw:
                        redraw = true;
                        break;
                }
            });

            if(redraw)
                redraw_desktop();
        }
    });
}

void gui::Desktop::redraw_desktop() {
    constexpr int32_t decoration_side_width = 3;
    constexpr int32_t decoration_top_width = 18;
    constexpr auto decoration_colour = Colour(200, 200, 200);

    std::lock_guard guard{compositor_lock};
            
    if(mouse.is_dragging) {
        if(!mouse.left_button_down) {
            mouse.is_dragging = false;
        } else {
            auto new_pos = mouse.pos + mouse.dragging_offset;
            new_pos.clamp(Vec2i{decoration_side_width, decoration_top_width + 20}, size - mouse.dragging_window->get_rect().size - Vec2i{decoration_side_width, decoration_side_width});

            mouse.dragging_window->set_pos(new_pos);
        }
    }

    for(auto it = windows.begin(); it != windows.end(); ++it) {
        auto* window = *it;
        auto rect = window->get_rect();
        Rect top_bar{rect.pos - Vec2i{decoration_side_width, decoration_top_width}, Vec2i{rect.size.x + 2 * decoration_side_width, decoration_top_width}};

        if(mouse.pos.collides_with(top_bar.pos, top_bar.size) && mouse.left_button_down && !mouse.is_dragging) {
            mouse.dragging_window = window;
            mouse.dragging_offset = rect.pos - mouse.pos;
            mouse.is_dragging = true;

            auto* it = windows.find(window);
            ASSERT(it != windows.end());
            windows.erase(it);
            windows.push_back(window);
            break; // iterators invalidated
        }
    }

    fb_canvas.blit_noalpha({0, 0}, {0, 0}, size, background.canvas());

    for(auto* window : windows) {
        // Draw decorations
        auto rect = window->get_rect();

        draw::rect(fb_canvas, rect.pos - Vec2i{decoration_side_width, 0}, Vec2i{decoration_side_width, rect.size.y}, decoration_colour); // Left bar
        draw::rect(fb_canvas, rect.pos + Vec2i{rect.size.x, 0}, Vec2i{decoration_side_width, rect.size.y}, decoration_colour); // Right bar

        draw::rect(fb_canvas, rect.pos + Vec2i{-decoration_side_width, rect.size.y}, Vec2i{rect.size.x + 2 * decoration_side_width, decoration_side_width}, decoration_colour); // Bottom bar

        Rect top_bar{rect.pos - Vec2i{decoration_side_width, decoration_top_width}, Vec2i{rect.size.x + 2 * decoration_side_width, decoration_top_width}};
        draw::rect(fb_canvas, top_bar, decoration_colour); // Top bar
        draw::text(fb_canvas, rect.pos - Vec2i{decoration_side_width, decoration_top_width - 1} + Vec2i{rect.size.x / 2, 0}, window->get_title(), Colour{0, 0, 0}, decoration_colour, draw::TextAlign::Center);

        // Actually blit screen
        fb_canvas.blit_noalpha(rect.pos, {0, 0}, rect.size, window->get_canvas());
    }

    // Draw topbar
    draw::rect(fb_canvas, Vec2i{0, 0}, Vec2i{(int)size.x, 20}, Colour{203, 45, 62});
    draw::text(fb_canvas, Vec2i{2, 2}, "Luna", Colour{255, 214, 191}, Colour{0, 0, 0, 0});

    // Draw mouse
    fb_canvas.blit(mouse.pos, {0, 0}, cursor.get_res(), cursor.canvas());

    gpu->flush();
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