#pragma once

#include <Luna/common.hpp>
#include <std/vector.hpp>

namespace usb {
    struct [[gnu::packed]] DeviceRequestPacket {
        uint8_t type;
        uint8_t request;
        uint16_t value;
        uint16_t index = 0;
        uint16_t length = 0;
    };

    namespace request_type {
        constexpr uint8_t host_to_device = (0 << 7);
        constexpr uint8_t device_to_host = (1 << 7);

        constexpr uint8_t to_standard = (0 << 5);
        constexpr uint8_t to_class = (1 << 5);
        constexpr uint8_t to_vendor = (2 << 5);

        constexpr uint8_t device = (0 << 0);
        constexpr uint8_t interface = (1 << 0);
        constexpr uint8_t endpoint = (2 << 0);
    } // namespace request_type

    namespace request_ops {
        constexpr uint8_t get_status = 0;
        constexpr uint8_t clear_feature = 1;
        constexpr uint8_t set_feature = 2;
        constexpr uint8_t set_address = 5;
        constexpr uint8_t get_descriptor = 6;
        constexpr uint8_t set_descriptor = 7;
        constexpr uint8_t get_configuration = 8;
        constexpr uint8_t set_configuration = 9;
        constexpr uint8_t get_interface = 10;
        constexpr uint8_t set_interface = 11;
    } // namespace request_ops

    namespace descriptor_types {
        constexpr uint8_t device = 1;
        constexpr uint8_t config = 2;
        constexpr uint8_t string = 3;
        constexpr uint8_t interface = 4;
        constexpr uint8_t endpoint = 5;

        constexpr uint8_t ep_companion = 0x30;
    } // namespace descriptor_types
    

    struct [[gnu::packed]] DeviceDescriptor {
        uint8_t length;
        uint8_t type;
        uint16_t usb_version;
        uint8_t device_class;
        uint8_t device_subclass;
        uint8_t device_protocol;
        uint8_t max_packet_size;
        uint16_t vendor_id;
        uint16_t product_id;
        uint16_t device_release;
        uint8_t manufacturer_str;
        uint8_t product_str;
        uint8_t serial_num_str;
        uint8_t num_configs;
    };

    struct [[gnu::packed]] ConfigDescriptor {
        uint8_t length;
        uint8_t type;
        uint16_t total_length;
        uint8_t n_interfaces;
        uint8_t config_val;
        uint8_t config_str;
        uint8_t attributes;
        uint8_t max_power;
    };

    struct [[gnu::packed]] InterfaceDescriptor {
        uint8_t length;
        uint8_t type;
        uint8_t num;
        uint8_t alternate_setting;
        uint8_t n_endpoints;
        uint8_t class_code;
        uint8_t subclass_code;
        uint8_t protocol;
        uint8_t interface_str;
    };

    namespace ep_type
    {
        constexpr uint8_t control = 0b00;
        constexpr uint8_t isoch = 0b01;
        constexpr uint8_t bulk = 0b10;
        constexpr uint8_t irq = 0b11;
    } // namespace ep_type
    

    struct [[gnu::packed]] EndpointDescriptor {
        uint8_t length;
        uint8_t type;
        
        uint8_t ep_num : 4;
        uint8_t reserved : 3;
        uint8_t dir : 1;

        uint8_t ep_type : 2;
        uint8_t reserved_0 : 6;

        uint16_t max_packet_size;
        uint8_t interval;
    };

    struct [[gnu::packed]] EndpointCompanion {
        uint8_t length;
        uint8_t type;
        uint8_t max_burst;
        uint8_t attributes;
        uint32_t bytes_per_interval;
    };

    struct [[gnu::packed]] StringLanguageDescriptor {
        uint8_t length;
        uint8_t type;
        uint16_t lang_ids[127];
    };

    struct [[gnu::packed]] StringUnicodeDescriptor {
        uint8_t length;
        uint8_t type;
        uint16_t str[127];
    };

    struct ControlXfer {
        DeviceRequestPacket packet;
        bool write;
        size_t len;
        uint8_t* buf = nullptr;
    };

    struct DeviceDriver {
        bool addressed;

        void* userptr;
        bool (*ep0_control_xfer)(void* userptr, const ControlXfer& xfer);
    };

    struct Interface {
        InterfaceDescriptor desc;
        std::vector<EndpointDescriptor> eps;
    };

    struct Configuration {
        ConfigDescriptor desc;
        std::vector<Interface> interfaces;
    };


    struct Device;
    
    struct Driver {
        const char* name;

        void (*init)(Device& device);

        struct {
            bool bind = false;
            uint16_t version = 0;
        } version = {};

        struct {
            bool bind = false;
            uint8_t class_code = 0, subclass_code = 0, prog_if = 0;
        } proto = {};
    };

    #define DECLARE_USB_DRIVER(driver) [[maybe_unused, gnu::used, gnu::section(".usb_drivers")]] static usb::Driver* usb_driver_##driver = &driver

    struct Device {
        DeviceDriver hci;
        Driver* driver;

        DeviceDescriptor device_descriptor;
        uint16_t langid;

        std::vector<Configuration> configs;

        uint8_t curr_config, curr_interface;

        uint8_t find_ep(bool in, uint8_t type);
    };


    void register_device(DeviceDriver& driver);
    void init();
} // namespace usb
