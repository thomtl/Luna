#include <Luna/gui/gui.hpp>
#include <Luna/gui/framework.hpp>

#include <Luna/gui/bmp_parser.hpp>
#include <Luna/fs/vfs.hpp>

using namespace gui;

char gui::keycode_to_char(const KeyCodes& code) {
    using enum KeyCodes;

    switch(code) {
        case A: return 'a';
        case B: return 'b';
        case C: return 'c';
        case D: return 'd';
        case E: return 'e';
        case F: return 'f';
        case G: return 'g';
        case H: return 'h';
        case I: return 'i';
        case J: return 'j';
        case K: return 'k';
        case L: return 'l';
        case M: return 'm';
        case N: return 'n';
        case O: return 'o';
        case P: return 'p';
        case Q: return 'q';
        case R: return 'r';
        case S: return 's';
        case T: return 't';
        case U: return 'u';
        case V: return 'v';
        case W: return 'w';
        case X: return 'x';
        case Y: return 'y';
        case Z: return 'z';
        default: return '?';
    }
}

std::optional<Image> gui::read_image(const char* path) {
    auto* file = vfs::get_vfs().open(path);
    if(!file) {
        print("gui: Couldn't open {}\n", path);
        return std::nullopt;
    }

    auto image = bmp_parser::parse_bmp(file);
    file->close();

    return image;
}

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
        auto image = read_image("A:/luna/assets/cursor.bmp");
        cursor = std::move(image.value());
    }

    spawn([this] {
        while(true) {
            bool redraw = false;
            bool mouse_click = false;

            event_queue.handle_await([&](CompositorEvent& event) {
                switch (event.type) {
                    using enum CompositorEvent::Type;
                    case MouseUpdate:
                        mouse_click = event.mouse.left_button_down && !mouse.left_button_down;

                        mouse.pos += event.mouse.pos;
                        mouse.pos.clamp({0, 0}, size - cursor.get_res());
                        mouse.left_button_down = event.mouse.left_button_down;

                        redraw = true;
                        break;

                    case KeyboardUpdate:
                        if(focused_window)
                            focused_window->send_event(gui::WindowEvent{.type = gui::WindowEvent::Type::KeyboardOp, .keyboard = {.op = event.keyboard.op, .code = event.keyboard.key}});
                        redraw = true;
                        break;

                    case WindowRedraw:
                        redraw = true;
                        break;
                }
            });

            if(redraw)
                redraw_desktop(mouse_click);
        }
    });
}

void gui::Desktop::redraw_desktop(bool mouse_click) {
    constexpr int32_t decoration_side_width = 3;
    constexpr int32_t decoration_top_width = 18;
    constexpr Colour unfocussed_decoration_colour{100, 100, 100};
    constexpr Colour focussed_decoration_colour{200,200,200};

    std::lock_guard guard{compositor_lock};
            
    if(mouse.is_dragging) {
        if(!mouse.left_button_down) {
            mouse.is_dragging = false;
        } else {
            auto new_pos = mouse.pos + mouse.dragging_offset;
            if(new_pos.y < (20 + decoration_top_width))
                new_pos.y = 20 + decoration_top_width;

            if(new_pos.x < decoration_side_width)
                new_pos.x = decoration_side_width;

            mouse.dragging_window->set_pos(new_pos);
        }
    }

    for(auto it = windows.rbegin(); it != windows.rend(); ++it) {
        auto* window = *it;
        auto rect = window->get_rect();
        Rect top_bar{rect.pos - Vec2i{decoration_side_width, decoration_top_width}, Vec2i{rect.size.x + 2 * decoration_side_width, decoration_top_width}};
        Rect focus_region{rect.pos - Vec2i{decoration_side_width, decoration_top_width}, rect.size + Vec2i{2 * decoration_side_width, decoration_top_width + decoration_side_width}};

        if(top_bar.collides_with(mouse.pos) && mouse_click) {
            mouse.dragging_window = window;
            mouse.dragging_offset = rect.pos - mouse.pos;
            mouse.is_dragging = true;

            // Don't break here, its important that the focussing code still runs
        }

        if(focus_region.collides_with(mouse.pos) && mouse_click) {
            auto* it = windows.find(window);
            ASSERT(it != windows.end());
            windows.erase(it);
            windows.push_back(window);

            if(focused_window != window) {
                if(focused_window)
                    focused_window->send_event(WindowEvent{.type = WindowEvent::Type::Unfocus, .none = {}});

                focused_window = window;
                focused_window->send_event(WindowEvent{.type = WindowEvent::Type::Focus, .none = {}});
            }

            break; // Iterators invalidated
        }
    }

    fb_canvas.blit_noalpha({0, 0}, {0, 0}, size, background.canvas());

    for(auto* window : windows) {
        // Draw decorations
        auto is_focussed = (window == focused_window);
        auto absolute_rect = window->get_rect();
        auto decoration_colour = is_focussed ? focussed_decoration_colour : unfocussed_decoration_colour;

        Rect rect = absolute_rect;
        bool clipped_right = false, clipped_bottom = false;
        if(!absolute_rect.is_within({{0,0}, size})) {
            Vec2i clipped_pos{clamp(absolute_rect.pos.x, 0, size.x), clamp(absolute_rect.pos.y, 20, size.y)};

            Vec2i clipped_size = absolute_rect.size;
            if((clipped_pos.x + absolute_rect.size.x) >= size.x) {
                clipped_size.x = size.x - clipped_pos.x;
                clipped_right = true;
            }

            if((clipped_pos.y + absolute_rect.size.y) >= size.y) {
                clipped_size.y = size.y - clipped_pos.y;
                clipped_bottom = true;
            }

            rect = Rect{clipped_pos, clipped_size};
        }

        if(is_focussed && rect.collides_with(mouse.pos)) {
            mouse_has_left_window = false;
            window->send_event(WindowEvent{.type = WindowEvent::Type::MouseOver, .mouse = {.pos = mouse.pos - rect.pos}});
        }

        if(is_focussed && !rect.collides_with(mouse.pos) && !mouse_has_left_window) {
            window->send_event(WindowEvent{.type = WindowEvent::Type::MouseExit, .none = {}});
            mouse_has_left_window = true;
        }

        if(is_focussed && mouse_click)
            window->send_event(WindowEvent{.type = WindowEvent::Type::MouseClick, .none = {}});

        draw::rect(fb_canvas, rect.pos - Vec2i{decoration_side_width, 0}, Vec2i{decoration_side_width, rect.size.y}, decoration_colour); // Left bar

        if(!clipped_right)
            draw::rect(fb_canvas, rect.pos + Vec2i{rect.size.x, 0}, Vec2i{decoration_side_width, rect.size.y}, decoration_colour); // Right bar

        if(!clipped_bottom)
            draw::rect(fb_canvas, rect.pos + Vec2i{-decoration_side_width, rect.size.y}, Vec2i{rect.size.x + 2 * decoration_side_width, decoration_side_width}, decoration_colour); // Bottom bar

        Rect top_bar{rect.pos - Vec2i{decoration_side_width, decoration_top_width}, Vec2i{rect.size.x + 2 * decoration_side_width, decoration_top_width}};
        draw::rect(fb_canvas, top_bar, decoration_colour); // Top bar
        draw::text(fb_canvas, rect.pos - Vec2i{decoration_side_width, decoration_top_width - 1} + Vec2i{absolute_rect.size.x / 2, 0}, window->get_title(), Colour{0, 0, 0}, decoration_colour, draw::TextAlign::Center);

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


static constinit Desktop* desktop = nullptr;

void gui::init() {
    auto& gpu = gpu::get_gpu();

    desktop = new Desktop{gpu};
}

Desktop& gui::get_desktop() {
    ASSERT(desktop);
    return *desktop;
}