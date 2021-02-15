#pragma once

#include <Luna/common.hpp>
#include <std/vector.hpp>
#include <Luna/drivers/usb/usb_spec.hpp>

namespace usb {
    struct ControlXfer {
        spec::DeviceRequestPacket packet;
        bool write;
        size_t len;
        uint8_t* buf = nullptr;
    };

    struct EndpointData {
        spec::EndpointDescriptor desc;
        spec::EndpointCompanion companion;
        spec::IsochEndpointCompanion isoch_companion;
    };

    struct Interface {
        spec::InterfaceDescriptor desc;
        std::vector<EndpointData> eps;
    };

    struct Configuration {
        spec::ConfigDescriptor desc;
        std::vector<Interface> interfaces;
    };


    struct Device;

    namespace match {
        constexpr uint32_t version = (1 << 0);
        constexpr uint32_t class_code = (1 << 1);
        constexpr uint32_t subclass_code = (1 << 2);
        constexpr uint32_t protocol_code = (1 << 3);
        constexpr uint32_t vendor_product = (1 << 4);
    } // namespace match

    struct Driver {
        const char* name;

        void (*init)(Device& device);

        uint32_t match;

        uint16_t version = 0;
        uint8_t class_code = 0, subclass_code = 0, protocol_code = 0;

        std::span<std::pair<uint16_t, uint16_t>> id_list = {};
    };

    #define DECLARE_USB_DRIVER(driver) [[maybe_unused, gnu::used, gnu::section(".usb_drivers")]] static usb::Driver* usb_driver_##driver = &driver

    struct DeviceDriver {
        bool addressed;

        void* userptr;
        bool (*setup_ep)(void* userptr, const EndpointData& ep);
        bool (*ep0_control_xfer)(void* userptr, const ControlXfer& xfer);
        bool (*ep_bulk_xfer)(void* userptr, uint8_t epid, std::span<uint8_t> data);
    };

    struct Endpoint {
        EndpointData data;
        Device* device;

        void xfer(std::span<uint8_t> data);
    };

    struct Device {
        DeviceDriver hci;
        Driver* driver;

        spec::DeviceDescriptor device_descriptor;
        uint16_t langid;

        std::vector<Configuration> configs;

        uint8_t curr_config, curr_interface;

        std::vector<Endpoint> endpoints;

        void configure();

        EndpointData find_ep(bool in, uint8_t type);
        Endpoint& setup_ep(const EndpointData& data);
    };


    void register_device(DeviceDriver& driver);
    void init_devices();
} // namespace usb
