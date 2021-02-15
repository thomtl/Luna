#include <Luna/common.hpp>
#include <Luna/drivers/usb/usb.hpp>
#include <Luna/misc/log.hpp>

constexpr uint32_t default_baud = 9600;

constexpr size_t clk = 48000000;
constexpr size_t clk_div(uint8_t ps, uint8_t factor) {
    return (1 << (12 - 3 * ps - factor));
}

constexpr size_t min_rate(uint8_t ps) {
    return clk / (clk_div(ps, 1) * 512);
}

constexpr size_t min_bps = div_ceil(clk, clk_div(0, 0) * 256);
constexpr size_t max_bps = clk / (clk_div(3, 0) * 2);


constexpr uint8_t req_read_version = 0x5F;
constexpr uint8_t req_serial_init = 0xA1;
constexpr uint8_t req_read_reg = 0x95;
constexpr uint8_t req_write_reg = 0x9A;
constexpr uint8_t req_modem_control = 0xA4;

constexpr uint8_t reg_prescaler = 0x12;
constexpr uint8_t reg_divisor = 0x13;
constexpr uint8_t reg_lcr = 0x18;
constexpr uint8_t reg_lcr2 = 0x25;

namespace lcr {
    constexpr uint8_t data5 = 0x0;
    constexpr uint8_t data6 = 0x1;
    constexpr uint8_t data7 = 0x2;
    constexpr uint8_t data8 = 0x3;

    constexpr uint8_t stop1 = (0 << 2);
    constexpr uint8_t stop2 = (1 << 2);

    constexpr uint8_t tx_enable = (1 << 7);
    constexpr uint8_t rx_enable = (1 << 6);
} // namespace lcr

namespace mcr {
    constexpr uint8_t rts = (1 << 6);
    constexpr uint8_t dtr = (1 << 5);
} // namespace mcr

struct Device {
    void send(std::span<uint8_t> data);
    void recv(std::span<uint8_t> data);

    void control_out(uint8_t op, uint16_t value = 0, uint16_t index = 0);
    void control_in(uint8_t op, std::span<uint8_t> xfer, uint16_t value = 0, uint16_t index = 0);

    void update_config(uint32_t baud, uint16_t lcr);
    void update_mcr(uint16_t mcr);
    uint8_t get_status();

    usb::Device* dev;

    uint16_t lcr, mcr;
    uint32_t baud;

    enum {
        quirkLimitedPrescaler = (1 << 0)
    };
    uint8_t quirks;

    usb::Endpoint* in, *out, *irq;
};

void Device::control_out(uint8_t op, uint16_t value, uint16_t index) {
    ASSERT(dev->hci.ep0_control_xfer(dev->hci.userptr, {.packet = {.type = usb::spec::request_type::host_to_device | usb::spec::request_type::to_vendor | usb::spec::request_type::device,
                                                                   .request = op,
                                                                   .value = value,
                                                                   .index = index},
                                                        .write = false,
                                                        .len = 0}));
}

void Device::control_in(uint8_t op, std::span<uint8_t> xfer, uint16_t value, uint16_t index) {
    ASSERT(dev->hci.ep0_control_xfer(dev->hci.userptr, {.packet = {.type = usb::spec::request_type::device_to_host | usb::spec::request_type::to_vendor | usb::spec::request_type::device,
                                                                   .request = op,
                                                                   .value = value,
                                                                   .index = index,
                                                                   .length = (uint16_t)xfer.size_bytes()},
                                                        .write = false,
                                                        .len = (uint16_t)xfer.size_bytes(),
                                                        .buf = xfer.data()}));
}

void Device::update_config(uint32_t baud, uint16_t lcr) {

    // Mostly taken from Linux, https://elixir.bootlin.com/linux/v5.11/source/drivers/usb/serial/ch341.c#L177
    auto get_divisor = [&](uint64_t baud) -> uint16_t {
        baud = clamp(baud, min_bps, max_bps);

        int factor = 1, ps = 3;
        for(; ps >= 0; ps--)
            if(baud > min_rate(ps))
                break;

        ASSERT(ps >= 0);

        auto clk_divisor = clk_div(ps, factor);
        auto div = clk / (clk_divisor * baud);
        

        bool force_fact0 = (ps < 3 && quirks & quirkLimitedPrescaler) ? true : false;
        
        if(div < 9 || div > 255 || force_fact0) {
            div /= 2;
            clk_divisor *= 2;
            factor = 0;
        }

        ASSERT(div >= 2);

        if((16 * clk / (clk_divisor * div) - 16 * baud) >= (16 * baud - 16 * clk / (clk_divisor * (div + 1))))
            div++;

        if(factor == 1 && (div % 2) == 0) {
            div /= 2;
            factor = 0;
        }

        return ((0x100 - div) << 8) | (factor << 2) | ps;
    };

    auto divisor = get_divisor(baud);
    divisor |= (1 << 7); // Immediately send out data, do not wait for a full endpoint buffer (32 bytes)

    control_out(req_write_reg, (reg_divisor << 8) | reg_prescaler, divisor);
    control_out(req_write_reg, (reg_lcr2 << 8) | reg_lcr, lcr);

    this->baud = baud;
    this->lcr = lcr;
}

void Device::update_mcr(uint16_t mcr) {
    control_out(req_modem_control, ~mcr);

    this->mcr = mcr;
}

uint8_t Device::get_status() {
    uint8_t status[2];
    control_in(req_read_reg, {status}, 0x0706);

    return ~status[0] & 0xF;
}


void Device::send(std::span<uint8_t> data) {
    out->xfer(data);
}

void Device::recv(std::span<uint8_t> data) {
    // Just xfer from the IN EP in 32 byte chunks

    (void)data;
    PANIC("TODO: Implement a sane recv");
}


static void init(usb::Device& dev) {
    auto* device = new Device{};
    device->dev = &dev;

    device->irq = &dev.setup_ep(dev.find_ep(true, usb::spec::ep_type::irq));
    device->out = &dev.setup_ep(dev.find_ep(false, usb::spec::ep_type::bulk));
    device->in = &dev.setup_ep(dev.find_ep(true, usb::spec::ep_type::bulk));
    dev.configure();

    uint8_t version[2];
    device->control_in(req_read_version, {version});
    print("usb/ch341: Detected device version: {:#x}\n", version[0]);

    device->control_out(req_serial_init);

    device->update_config(default_baud, lcr::rx_enable | lcr::tx_enable | lcr::data8);
    device->update_mcr(0);

    device->get_status();
    device->update_mcr(mcr::rts | mcr::dtr);

    // TODO: What now? Do we mount it in some kind of devfs?
}

static std::pair<uint16_t, uint16_t> ch341_ids[] = {
    {0x1A86, 0x5512},
    {0x1A86, 0x5523},
    {0x1A86, 0x7522},
    {0x1A86, 0x7523},
    {0x4348, 0x5523}
};

static usb::Driver driver = {
    .name = "CH341 USB-to-RS232 driver",
    .init = init,
    .match = usb::match::vendor_product | usb::match::class_code,
    
    .class_code = 0xFF, // Vendor Specific
    .id_list = {ch341_ids}
};
DECLARE_USB_DRIVER(driver);