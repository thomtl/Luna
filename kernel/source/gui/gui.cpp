#include <Luna/gui/gui.hpp>
#include <Luna/gui/basic.hpp>
#include <Luna/gui/log_window.hpp>

using namespace gui;

struct TopBar : public Widget {
    TopBar(const gpu::Mode& mode): rect{{0, 0}, {(int32_t)mode.width, 20}, bar_colour}, text{{2, 2}, "Luna", text_colour, Colour{0, 0, 0, 0}} {

    }

    void redraw(Desktop& desktop, const Vec2i& parent_pos) {
        rect.redraw(desktop, parent_pos);
        text.redraw(desktop, parent_pos);
    }

    private:
    static constexpr Colour bar_colour = Colour{203, 45, 62};
    static constexpr Colour text_colour = Colour{255, 214, 191};

    Rect rect;
    Text text;
};

static Desktop* desktop;

void gui::init() {
    auto& gpu = gpu::get_gpu();

    desktop = new Desktop{gpu};

    auto* bar = new TopBar{gpu.get_mode()};
    desktop->add_window(bar);
}

Desktop& gui::get_desktop() {
    ASSERT(desktop);
    return *desktop;
}