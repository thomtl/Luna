#include <Luna/common.hpp>
#include <Luna/drivers/usb/usb.hpp>
#include <Luna/misc/log.hpp>

#include <Luna/gui/gui.hpp>

#include <std/array.hpp>
#include <std/event_queue.hpp>

constexpr uint8_t mod_lcontrol = (1 << 0);
constexpr uint8_t mod_lshift = (1 << 1);
constexpr uint8_t mod_lalt = (1 << 2);
constexpr uint8_t mod_lgui = (1 << 3);
constexpr uint8_t mod_rcontrol = (1 << 4);
constexpr uint8_t mod_rshift = (1 << 5);
constexpr uint8_t mod_ralt = (1 << 6);
constexpr uint8_t mod_rgui = (1 << 7);

struct [[gnu::packed]] BootReport {
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keys[6];
};
static_assert(sizeof(BootReport) == 8);

constexpr uint8_t get_report_cmd = 0x1;
constexpr uint8_t get_idle_cmd = 0x2;
constexpr uint8_t get_protocol_cmd = 0x3;
constexpr uint8_t set_report_cmd = 0x9;
constexpr uint8_t set_idle_cmd = 0xA;
constexpr uint8_t set_protocol_cmd = 0xB;

struct HIDKeyboardDevice {
    usb::Device* usb_dev;
    usb::Endpoint* in;

    std::EventQueue<gui::CompositorEvent>* queue;

    void set_idle();

    gui::KeyCodes key_code[6];
    bool key_pressed[6] = {false};

    uint8_t old_modifiers;
};


void HIDKeyboardDevice::set_idle() {
    usb_dev->hci.ep0_control_xfer(usb_dev->hci.userptr, {.packet = {.type = usb::spec::request_type::host_to_device | usb::spec::request_type::to_class | usb::spec::request_type::interface, 
                                                                    .request = set_idle_cmd,
                                                                    .value = 0,
                                                                    .index = usb_dev->curr_interface
                                                                    },
                                                                    .write = false,
                                                                    .len = 0});
}


using enum gui::KeyCodes;
constexpr gui::KeyCodes key_loopup_table[] = {
    Unknown, Unknown, Unknown, Unknown,
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    _1, _2, _3, _4, _5, _6, _7, _8, _9, _0,
    Enter, Escape, BackSpace, Tab, Space, Minus, Equals, BraceOpen, BraceClose, BackSlash, Tilde, Semicolon,
    Unknown, Tilde, Comma, Dot, Slash, CapsLock, 
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    PrntScrn, ScrollLock, Pause, Insert, Home, PageUp, Delete, End, 
    PageDown, RightArrow, LeftArrow, DownArrow, UpArrow, NumLock,
    Slash, Star, Minus, Plus, Enter, _1, _2, _3, _4, _5, _6, _7, _8, _9, _0, Dot, BackSlash, Unknown, Unknown, Equals, // Keypad
    F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24,
    Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, 
    Unknown, Unknown, Unknown, Unknown, Unknown, 
    Comma, Equals, // Keypad
    Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown,
    Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown,
    Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown,
    Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown,
    Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, 
    ParenOpen, ParenClose, BraceOpen, BraceClose, Tab, BackSpace, A, B, C, D, E, F, // Keypad
    Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown,
    Space, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown,
    Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown,
    Unknown, Unknown, // Reserved
    Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown,
    Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, 
    Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, Unknown, 
    Unknown, Unknown, Unknown, Unknown, 
};
static_assert(key_loopup_table[58] == F1);
static_assert(key_loopup_table[80] == LeftArrow);
static_assert(key_loopup_table[96] == _8);
static_assert(key_loopup_table[104] == F13);
static_assert(key_loopup_table[115] == F24);
static_assert(key_loopup_table[134] == Equals);
static_assert(key_loopup_table[182] == ParenOpen);
static_assert(key_loopup_table[193] == F);
static_assert(key_loopup_table[205] == Space);


static void init(usb::Device& device) {
    auto* dev = new HIDKeyboardDevice{};
    dev->usb_dev = &device;
    dev->queue = &gui::get_desktop().get_event_queue();

    const auto in_data = dev->usb_dev->find_ep(true, usb::spec::ep_type::irq);

    dev->in = &dev->usb_dev->setup_ep(in_data);

    dev->usb_dev->configure();

    print("usb/hid_keyboard: IN EP{}\n", in_data.desc.ep_num);

    dev->set_idle();

    uint8_t packet_size = in_data.desc.max_packet_size; // TODO: Can we assume this?
    ASSERT(packet_size <= 8); // TODO

    spawn([dev, packet_size] {
        auto* buf = new uint8_t[packet_size];
        std::span<uint8_t> data{buf, packet_size};

        while(true) {
            dev->in->xfer(data)->await();

            auto report = *(BootReport*)buf;

            if(report.keys[0] == 1) {
                print("usb/hid_keyboard: Key Rollover\n");
                continue;
            }

            //print("{:#x} {:#x} {:#x} {:#x} {:#x} {:#x} {:#x}\n", report.modifiers, report.keys[0], report.keys[1], report.keys[2], report.keys[3], report.keys[4], report.keys[5]);

            #define CHECK_MODIFER(bit, code) \
                if(!(dev->old_modifiers & bit) && (report.modifiers & bit)) \
                    dev->queue->push(gui::CompositorEvent{.type = gui::CompositorEvent::Type::KeyboardUpdate, .keyboard = {.op = gui::KeyOp::Press, .key = code}}); \
                if((dev->old_modifiers & bit) && !(report.modifiers & bit)) \
                    dev->queue->push(gui::CompositorEvent{.type = gui::CompositorEvent::Type::KeyboardUpdate, .keyboard = {.op = gui::KeyOp::Unpress, .key = code}})

            CHECK_MODIFER(mod_lshift, LeftShift);
            CHECK_MODIFER(mod_rshift, RightShift);

            CHECK_MODIFER(mod_lcontrol, LeftControl);
            CHECK_MODIFER(mod_rcontrol, RightControl);

            CHECK_MODIFER(mod_lalt, LeftAlt);
            CHECK_MODIFER(mod_ralt, RightAlt);
            
            CHECK_MODIFER(mod_lgui, LeftGUI);
            CHECK_MODIFER(mod_rgui, RightGUI);

            dev->old_modifiers = report.modifiers;


            for(size_t i = 0; i < 6; i++) {
                auto& pressed = dev->key_pressed[i];
                auto& saved_code = dev->key_code[i];
                auto code = key_loopup_table[report.keys[i]];

                if(report.keys[i] != 0) {
                    if(!pressed) {
                        dev->queue->push(gui::CompositorEvent{.type = gui::CompositorEvent::Type::KeyboardUpdate, .keyboard = {.op = gui::KeyOp::Press, .key = code}});

                        saved_code = code;
                        pressed = true;
                    } else {
                        if(code == saved_code) {
                            dev->queue->push(gui::CompositorEvent{.type = gui::CompositorEvent::Type::KeyboardUpdate, .keyboard = {.op = gui::KeyOp::Repeat, .key = code}});
                        } else {
                            dev->queue->push(gui::CompositorEvent{.type = gui::CompositorEvent::Type::KeyboardUpdate, .keyboard = {.op = gui::KeyOp::Unpress, .key = saved_code}});
                            dev->queue->push(gui::CompositorEvent{.type = gui::CompositorEvent::Type::KeyboardUpdate, .keyboard = {.op = gui::KeyOp::Press, .key = code}});

                            saved_code = code;
                        }
                    }
                } else {
                    if(dev->key_pressed[i])
                        dev->queue->push(gui::CompositorEvent{.type = gui::CompositorEvent::Type::KeyboardUpdate, .keyboard = {.op = gui::KeyOp::Unpress, .key = saved_code}});

                    pressed = false;
                }
            }
        }

        delete[] buf;
    });
}

static usb::Driver driver = {
    .name = "USB Boot Protocol Keyboard Driver",
    .init = init,
    .match = usb::match::class_code | usb::match::subclass_code | usb::match::protocol_code,
    
    .class_code = 0x3, // HID
    .subclass_code = 0x1, // Boot Interface
    .protocol_code = 0x1, // Keyboard
};
DECLARE_USB_DRIVER(driver);