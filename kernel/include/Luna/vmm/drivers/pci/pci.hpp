#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

#include <Luna/misc/format.hpp>

#include <std/unordered_map.hpp>

namespace vm::pci {
    union [[gnu::packed]] DeviceID {
        struct {
            uint16_t func : 3;
            uint16_t slot : 5;
            uint16_t bus : 8;
        };
        uint16_t raw;
    };

    struct AbstractPCIDriver;

    struct AbstractPCIAccess {
        virtual ~AbstractPCIAccess() {}

        virtual void register_pci_driver(DeviceID device, AbstractPCIDriver* driver) = 0;
    };

    struct AbstractPCIDriver {
        virtual ~AbstractPCIDriver() {}

        virtual void register_pci_driver(AbstractPCIAccess* vm) = 0;

        virtual void pci_write(const DeviceID dev, uint16_t reg, uint32_t value, uint8_t size) = 0;
        virtual uint32_t pci_read(const DeviceID dev, uint16_t reg, uint8_t size) = 0;
    };

   struct [[gnu::packed]] ConfigSpaceHeader {
        uint16_t vendor_id, device_id;
        uint16_t command, status;
        uint8_t revision, prog_if, subclass, class_id;
        uint8_t cache_line_size, latency_timer, header_type, bist;
        uint32_t bar[6];
        uint32_t cardbus_cis_ptr;
        uint16_t subsystem_vendor_id, subsystem_device_id;
        uint32_t expansion_rom_base;
        uint8_t capabilities, reserved, reserved_0, reserved_1;
        uint32_t reserved_2;
        uint8_t irq_line, irq_pin, min_grant, max_latency;
    };

    union [[gnu::packed]] ConfigSpace {
        ConfigSpaceHeader header;
        uint8_t data8[256];
        uint16_t data16[128];
        uint32_t data32[64];
    };
    static_assert(sizeof(ConfigSpace) == 256);
} // namespace vm::pci
