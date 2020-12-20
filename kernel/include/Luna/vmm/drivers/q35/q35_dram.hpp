#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

#include <Luna/misc/format.hpp>

#include <Luna/vmm/drivers/pci/pci.hpp>

namespace vm::q35::dram {
    constexpr uint16_t cap_off = 0xE0;

    constexpr uint16_t pam0 = 0x90;
    constexpr uint16_t pam_size = 7;
    constexpr uint16_t n_pam = 13;

    constexpr struct {
        uintptr_t base, limit;
    } pam_regions[] = {
        {.base = 0xF'0000, .limit = 0xF'FFFF}, // PAM0 hi

        {.base = 0xC'0000, .limit = 0xC'3FFF}, // PAM1 lo
        {.base = 0xC'4000, .limit = 0xC'7FFF}, //      hi
        {.base = 0xC'8000, .limit = 0xC'BFFF}, // PAM2 lo
        {.base = 0xC'C000, .limit = 0xC'FFFF}, //      hi

        {.base = 0xD'0000, .limit = 0xD'3FFF}, // PAM3 lo
        {.base = 0xD'4000, .limit = 0xD'7FFF}, //      hi
        {.base = 0xD'8000, .limit = 0xD'BFFF}, // PAM4 lo
        {.base = 0xD'C000, .limit = 0xD'FFFF}, //      hi

        {.base = 0xE'0000, .limit = 0xE'3FFF}, // PAM5 lo
        {.base = 0xE'4000, .limit = 0xE'7FFF}, //      hi
        {.base = 0xE'8000, .limit = 0xE'BFFF}, // PAM6 lo
        {.base = 0xE'C000, .limit = 0xE'FFFF}, //      hi
    };

    struct Driver : public vm::AbstractDriver, public vm::pci::AbstractPCIDriver {
        Driver() {
            space.header.vendor_id = 0x8086;
            space.header.device_id = 0x29C0;

            space.header.command = (1 << 1);
            space.header.status = (1 << 4) | (1 << 7);

            space.header.revision = 2;

            space.header.class_id = 6;
            space.header.subclass = 0;
            space.header.prog_if = 0;

            space.header.capabilities = 0xE0;

            space.header.subsystem_vendor_id = 0x1AF4;
            space.header.subsystem_device_id = 0x1100;

            space.data8[cap_off] = 0b1001; // Vendor dependent
            space.data8[cap_off + 1] = 0; // No next cap
            space.data8[cap_off + 2] = 0xB; // Length
            space.data8[cap_off + 3] = 1; // Low Nybble = version
            // Rest of the cap fields are 0

            // Initialize PAM registers, TODO? Is this correct, everything is read-only
            space.data8[pam0] = 0x10;
            space.data8[pam0 + 1] = 0x11;
            space.data8[pam0 + 2] = 0x11;
            space.data8[pam0 + 3] = 0x11;
            space.data8[pam0 + 4] = 0x11;
            space.data8[pam0 + 5] = 0x11;
            space.data8[pam0 + 6] = 0x11;
        }

        void register_driver([[maybe_unused]] Vm* vm) { }

        void register_pci_driver(vm::pci::AbstractPCIAccess* bus) {
            bus->register_pci_driver(vm::pci::DeviceID{.raw = 0}, this); // Bus 0, Slot 0, Func 0
        }

        void pio_write(uint16_t port, uint32_t value, [[maybe_unused]] uint8_t size) {
            print("q35::dram: Unhandled PIO write, port: {:#x}, value: {:#x}\n", port, value);
        }

        uint32_t pio_read(uint16_t port, [[maybe_unused]] uint8_t size) {
            print("q35::dram: Unhandled PIO read, port: {:#x}\n", port);

            return 0;
        }

        void pci_write([[maybe_unused]] const vm::pci::DeviceID dev, uint16_t reg, uint32_t value, uint8_t size) {
            switch (size) {
                case 1: space.data8[reg] = value; break;
                case 2: space.data16[reg / 2] = value; break;
                case 4: space.data32[reg / 4] = value; break;
                default: PANIC("Unknown PCI Access size");
            }

            if(ranges_overlap(reg, size, 0, sizeof(pci::ConfigSpaceHeader)))
                ; // TODO: Handle header accesses
            else if(ranges_overlap(reg, size, pam0, pam_size))
                pam_update();
            else
                print("q35::dram: Unhandled PCI write, reg: {:#x}, value: {:#x}\n", reg, value);
        }

        uint32_t pci_read([[maybe_unused]] const vm::pci::DeviceID dev, uint16_t reg, uint8_t size) {
            uint32_t ret = 0;
            switch (size) {
                case 1: ret = space.data8[reg]; break;
                case 2: ret = space.data16[reg / 2]; break;
                case 4: ret = space.data32[reg / 4]; break;
                default: PANIC("Unknown PCI Access size");
            }

            if(ranges_overlap(reg, size, 0, sizeof(pci::ConfigSpaceHeader)))
                ; // Nothing special to do here
            else if(ranges_overlap(reg, size, pam0, pam_size))
                ; // Nothing special to do here
            else
                print("q35::dram: Unhandled PCI read, reg: {:#x}, size: {:#x}\n", reg, (uint16_t)size);

            return ret;
        }

        void pam_update() {
            // TODO: Handle PAMs correctly, update r/w status
            /*for(size_t i = 0; i < 13; i++) {
                auto pam = (space.data8[pam0 + div_ceil(i, 2)] >> ((!(i & 1)) * 4)) & 0b11;

                print("PAM{}, {:#x} -> {:#x}, {:#b}\n", i, pam_regions[i].base, pam_regions[i].limit, (uint16_t)pam) ;
            }*/
        }

        pci::ConfigSpace space;
    };
} // namespace vm::q35::dram
