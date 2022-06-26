#include <Luna/common.hpp>
#include <Luna/drivers/usb/usb.hpp>
#include <Luna/misc/log.hpp>

#include <Luna/gui/gui.hpp>

#include <std/event_queue.hpp>

struct [[gnu::packed]] BootReport {
    uint8_t buttons;
    int8_t x;
    int8_t y;
};

constexpr uint8_t get_report_cmd = 0x1;
constexpr uint8_t get_idle_cmd = 0x2;
constexpr uint8_t get_protocol_cmd = 0x3;
constexpr uint8_t set_report_cmd = 0x9;
constexpr uint8_t set_idle_cmd = 0xA;
constexpr uint8_t set_protocol_cmd = 0xB;

struct Device {
    usb::Device* usb_dev;
    usb::Endpoint* in;

    std::EventQueue<gui::CompositorEvent>* queue;

    void set_idle();
};


void Device::set_idle() {
    usb_dev->hci.ep0_control_xfer(usb_dev->hci.userptr, {.packet = {.type = usb::spec::request_type::host_to_device | usb::spec::request_type::to_class | usb::spec::request_type::interface, 
                                                                    .request = set_idle_cmd,
                                                                    .value = 0,
                                                                    .index = usb_dev->curr_interface
                                                                    },
                                                                    .write = false,
                                                                    .len = 0});
}

static void init(usb::Device& device) {
    auto* dev = new Device{};
    dev->usb_dev = &device;
    dev->queue = &gui::get_desktop().get_event_queue();

    const auto in_data = dev->usb_dev->find_ep(true, usb::spec::ep_type::irq);

    dev->in = &dev->usb_dev->setup_ep(in_data);

    dev->usb_dev->configure();

    print("usb/hid_mouse: IN EP{}\n", in_data.desc.ep_num);

    dev->set_idle();

    uint8_t packet_size = in_data.desc.max_packet_size; // TODO: Can we assume this?
    ASSERT(packet_size <= 8); // TODO

    spawn([dev, packet_size] {
        auto* buf = new uint8_t[packet_size];
        std::span<uint8_t> data{buf, packet_size};

        while(true) {
            dev->in->xfer(data)->await();

            auto report = *(BootReport*)buf;

            gui::CompositorEvent event{};
            event.type = gui::CompositorEvent::Type::MouseUpdate;
            event.mouse.pos = {report.x, report.y};
            event.mouse.left_button_down = report.buttons & 1;
            dev->queue->push(event);
        }

        delete[] buf;
    });
}

static usb::Driver driver = {
    .name = "USB Boot Protocol Mouse Driver",
    .init = init,
    .match = usb::match::class_code | usb::match::subclass_code | usb::match::protocol_code,
    
    .class_code = 0x3, // HID
    .subclass_code = 0x1, // Boot Interface
    .protocol_code = 0x2, // Mouse
};
DECLARE_USB_DRIVER(driver);