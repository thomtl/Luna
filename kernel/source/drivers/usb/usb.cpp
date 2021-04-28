#include <Luna/drivers/usb/usb.hpp>
#include <Luna/misc/log.hpp>

using namespace usb::spec;

static void set_configuration(usb::Device& dev, uint8_t n) {
    auto v = dev.hci.ep0_control_xfer(dev.hci.userptr, {.packet = {.type = request_type::host_to_device | request_type::to_standard | request_type::device, 
                                                  .request = request_ops::set_configuration,
                                                  .value = n},
                                        .write = false,
                                        .len = 0});

    ASSERT(v);
}

/*static void set_interface(usb::Device& dev, uint8_t n) {
    auto v = dev.hci.ep0_control_xfer(dev.hci.userptr, {.packet = {.type = usb::request_type::host_to_device | usb::request_type::to_standard | usb::request_type::device, 
                                                  .request = usb::request_ops::set_interface,
                                                  .value = n},
                                        .write = false,
                                        .len = 0});

    ASSERT(v);
}*/

static void get_descriptor(usb::Device& dev, uint16_t len, uint8_t* buf, uint8_t type, uint8_t index = 0, uint16_t language_id = 0) {
    auto v = dev.hci.ep0_control_xfer(dev.hci.userptr, {.packet = {.type = request_type::device_to_host | request_type::to_standard | request_type::device, 
                                                  .request = request_ops::get_descriptor,
                                                  .value = (uint16_t)((type << 8) | index),
                                                  .index = language_id,
                                                  .length = len},
                                        .write = false,
                                        .len = len,
                                        .buf = buf});

    ASSERT(v);
}

static void get_configuration(usb::Device& dev, uint8_t i) {
    auto& config = dev.configs[i];

    ConfigDescriptor desc{};
    get_descriptor(dev, sizeof(ConfigDescriptor), (uint8_t*)&desc, descriptor_types::config, i);
    config.desc = desc;

    auto* buf = new uint8_t[desc.total_length];
    get_descriptor(dev, desc.total_length, (uint8_t*)buf, descriptor_types::config, i);

    size_t off = desc.length;

    usb::Interface* curr_interface = nullptr;
    usb::EndpointData* curr_ep = nullptr;

    auto consume = [&] {
        const auto* header = (DescriptorHeader*)(buf + off);
        switch (header->type) { // TODO: Parse interface association descriptors
        case descriptor_types::interface:
            curr_interface = &config.interfaces.emplace_back();
            curr_interface->desc = *(const InterfaceDescriptor*)header;
            break;

        case descriptor_types::endpoint:
            curr_ep = &curr_interface->eps.emplace_back();
            curr_ep->desc = *(const EndpointDescriptor*)header;
            break;

        case descriptor_types::ss_ep_companion:
            ASSERT(curr_ep); // Dangling EP Companion?

            curr_ep->companion = *(const EndpointCompanion*)header;
            break;

        case descriptor_types::ssp_ep_isoch_companion:
            ASSERT(curr_ep); // Dangling EP Companion?

            curr_ep->isoch_companion = *(const IsochEndpointCompanion*)header;
            break;
        
        default:
            //print("usb: Unknown descriptor {} in configuration, skipping\n", header->type);
            break;
        }

        off += header->length;
    };

    while(off < desc.total_length)
        consume();

    delete[] buf;
}

static void print_string(usb::Device& dev, uint8_t i, const char* prefix, uint32_t alternative) {
    if(i == 0) {
        print("{}{}\n", prefix, alternative);
    } else {
        StringUnicodeDescriptor str{};
        get_descriptor(dev, 2, (uint8_t*)&str, descriptor_types::string, i, dev.langid); // First the the size
        get_descriptor(dev, str.length, (uint8_t*)&str, descriptor_types::string, i, dev.langid); // Now everything

        print("{}", prefix);
        for(int i = 0; i < (str.length - 2) / 2; i++) {
            auto c = (char)str.str[i];
            print("{}", c);
        }
        print("\n");
    }
}

static std::vector<usb::Device> devices;

void usb::register_device(usb::DeviceDriver& driver) {
    auto& dev = devices.emplace_back();
    dev.hci = driver;

    if(!dev.hci.addressed)
        PANIC("TODO: Send ADDRESS_DEVICE packet");
    
    get_descriptor(dev, sizeof(DeviceDescriptor), (uint8_t*)&dev.device_descriptor, descriptor_types::device);
    
    dev.configs.resize(dev.device_descriptor.num_configs);
    for(size_t i = 0; i < dev.device_descriptor.num_configs; i++)
        get_configuration(dev, i);
}

extern "C" uintptr_t _usb_drivers_start;
extern "C" uintptr_t _usb_drivers_end;

void usb::init_devices() {
    auto* start = (Driver**)&_usb_drivers_start;
    auto* end = (Driver**)&_usb_drivers_end;
    size_t size = end - start;
    auto find = [&](usb::Device& dev, uint8_t class_code, uint8_t subclass_code, uint8_t protocol_code) -> Driver* {
        for(size_t i = 0; i < size; i++) {
            auto& driver = *start[i];

            if(driver.match == 0)
                continue;

            if(driver.match & match::version && driver.version != dev.device_descriptor.usb_version)
                continue;

            if(driver.match & match::class_code && driver.class_code != class_code)
                continue;

            if(driver.match & match::subclass_code && driver.subclass_code != subclass_code)
                continue;

            if(driver.match & match::protocol_code && driver.protocol_code != protocol_code)
                continue;

            if(driver.match & match::vendor_product) {
                bool found = false;
                for(const auto [vid, pid] : driver.id_list) {
                    if(dev.device_descriptor.vendor_id == vid && dev.device_descriptor.product_id == pid) {
                        found = true;
                        break;
                    }
                }
                
                if(!found)
                    continue;
            }

            
            return &driver;
        }

        return nullptr;
    };

    for(auto& dev : devices) {
        StringLanguageDescriptor lang{};
        get_descriptor(dev, 2, (uint8_t*)&lang, descriptor_types::string, 0); // First the the size
        get_descriptor(dev, lang.length, (uint8_t*)&lang, descriptor_types::string, 0); // Now everything
        dev.langid = lang.lang_ids[0]; // Just pick the first one for now

        print("usb: Registered USB {:x}.{:x} Device\n", dev.device_descriptor.usb_version >> 8, dev.device_descriptor.usb_version & 0xFF);

        auto vid = dev.device_descriptor.vendor_id, pid = dev.device_descriptor.product_id;
        print_string(dev, dev.device_descriptor.manufacturer_str, "     Vendor: ", vid);
        print_string(dev, dev.device_descriptor.product_str, "     Product: ", pid);

        for(size_t i = 0; i < dev.configs.size(); i++) {
            print_string(dev, dev.configs[i].desc.config_str, "     Config: ", dev.configs[i].desc.config_val);

            for(size_t j = 0; j < dev.configs[i].interfaces.size(); j++) {
                auto& interface = dev.configs[i].interfaces[j];
                print_string(dev, interface.desc.interface_str, "             Interface: ", interface.desc.num);
                print("             ID: {:x}.{:x}.{:x}\n", interface.desc.class_code, interface.desc.subclass_code, interface.desc.protocol);


                auto* driver = find(dev, interface.desc.class_code, interface.desc.subclass_code, interface.desc.protocol);
                if(driver) {
                    dev.curr_config = i;
                    dev.curr_interface = j;
                    dev.driver = driver;

                    print("     Driver: {}\n", driver->name);

                    set_configuration(dev, dev.configs[i].desc.config_val);
                    
                    ASSERT(dev.driver->init);
                    dev.driver->init(dev);

                    goto found;
                }
            }
        }

        found:
        ;
    }
}

void usb::Device::configure() {
    set_configuration(*this, configs[curr_config].desc.config_val);
}

usb::EndpointData usb::Device::find_ep(bool in, uint8_t type) {
    auto& interface = configs[curr_config].interfaces[curr_interface];

    for(auto& ep : interface.eps)
        if(ep.desc.dir == in && ep.desc.ep_type == type)
            return ep;

    PANIC("Was not able to find endpoint\n");
}

usb::Endpoint& usb::Device::setup_ep(const EndpointData& data) {
    auto& interface = configs[curr_config].interfaces[curr_interface];

    for(auto& ep : interface.eps) {
        if(ep.desc.dir == data.desc.dir && ep.desc.ep_num == data.desc.ep_num) {
            ASSERT(hci.setup_ep(hci.userptr, ep));

            auto& ctx = endpoints.emplace_back();
            ctx.data = ep;
            ctx.device = this;
            
            return ctx;
        }
    }

    PANIC("Unable to setup EP");
}

std::unique_ptr<Promise<bool>> usb::Endpoint::xfer(std::span<uint8_t> xfer) {
    if(data.desc.ep_type == spec::ep_type::bulk)
        return device->hci.ep_bulk_xfer(device->hci.userptr, (2 * data.desc.ep_num) + data.desc.dir, xfer);
    else if(data.desc.ep_type == spec::ep_type::irq)
        return device->hci.ep_irq_xfer(device->hci.userptr, (2 * data.desc.ep_num) + data.desc.dir, xfer);
    else
        PANIC("Unknown EP type");
}