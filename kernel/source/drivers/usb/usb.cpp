#include <Luna/drivers/usb/usb.hpp>
#include <Luna/misc/log.hpp>

/*static void set_configuration(usb::Device& dev, uint8_t n) {
    auto v = dev.driver.ep0_control_xfer(dev.driver.userptr, {.packet = {.type = usb::request_type::host_to_device | usb::request_type::to_standard | usb::request_type::device, 
                                                  .request = usb::request_ops::set_configuration,
                                                  .value = n},
                                        .write = false,
                                        .len = 0});

    ASSERT(v);
}

static void set_interface(usb::Device& dev, uint8_t n) {
    auto v = dev.driver.ep0_control_xfer(dev.driver.userptr, {.packet = {.type = usb::request_type::host_to_device | usb::request_type::to_standard | usb::request_type::device, 
                                                  .request = usb::request_ops::set_interface,
                                                  .value = n},
                                        .write = false,
                                        .len = 0});

    ASSERT(v);
}*/

static void get_descriptor(usb::Device& dev, uint16_t len, uint8_t* buf, uint8_t type, uint8_t index = 0, uint16_t language_id = 0) {
    auto v = dev.driver.ep0_control_xfer(dev.driver.userptr, {.packet = {.type = usb::request_type::device_to_host | usb::request_type::to_standard | usb::request_type::device, 
                                                  .request = usb::request_ops::get_descriptor,
                                                  .value = (uint16_t)((type << 8) | index),
                                                  .index = language_id,
                                                  .length = len},
                                        .write = false,
                                        .len = len,
                                        .buf = buf});

    ASSERT(v);
}

static void print_string(usb::Device& dev, uint8_t i) {
    usb::StringUnicodeDescriptor str{};
    get_descriptor(dev, 2, (uint8_t*)&str, usb::descriptor_types::string, i, dev.langid); // First the the size
    get_descriptor(dev, str.length, (uint8_t*)&str, usb::descriptor_types::string, i, dev.langid); // Now everything

    for(int i = 0; i < (str.length - 2) / 2; i++) {
        auto c = (char)str.str[i];
        print("{}", c);
    }
}

void usb::register_device(usb::DeviceDriver& driver) {
    auto* dev = new Device{};
    dev->driver = driver;

    if(!dev->driver.addressed)
        PANIC("TODO: Send ADDRESS_DEVICE packet");
    
    usb::DeviceDescriptor desc{};
    get_descriptor(*dev, sizeof(DeviceDescriptor), (uint8_t*)&desc, descriptor_types::device);
    
    auto major = (desc.usb_version >> 8) & 0xFF;
    auto minor = desc.usb_version & 0xFF;
    auto vid = desc.vendor_id, did = desc.product_id;


    usb::StringLanguageDescriptor lang{};
    get_descriptor(*dev, 2, (uint8_t*)&lang, descriptor_types::string, 0); // First the the size
    get_descriptor(*dev, lang.length, (uint8_t*)&lang, descriptor_types::string, 0); // Now everything
    dev->langid = lang.lang_ids[0]; // Just pick the first one for now

    print("usb: Registered USB {}.{} Device\n", major, minor);

    print("     Vendor: ");
    if(desc.manufacturer_str) print_string(*dev, desc.manufacturer_str);
    else print("{:#x}", vid);
    print("\n");

    print("     Product: ");
    if(desc.product_str) print_string(*dev, desc.product_str);
    else print("{:#x}", did);
    print("\n");
}