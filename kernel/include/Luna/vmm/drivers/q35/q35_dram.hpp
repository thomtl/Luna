#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

#include <Luna/misc/format.hpp>

#include <Luna/vmm/drivers/pci/pci.hpp>

namespace vm::q35::dram {
    constexpr uint16_t pam0 = 0x90;
    constexpr uint16_t pam_size = 7;
    constexpr uint16_t n_pam = 13;

    struct Driver : public vm::AbstractDriver, public vm::pci::AbstractPCIDriver {
        Driver() {
            space.vendor_id = 0x8086;
            space.device_id = 0x29C0;

            space.subsystem_vendor_id = 0x1AF4;
            space.subsystem_device_id = 0x1100;

            space.data8[pam0] = 0x10; // Lower ROM is present
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

            if(ranges_overlap(reg, size, pam0, pam_size))
                pam_update();

            //print("q35::dram: Unhandled PCI write, reg: {:#x}, value: {:#x}\n", reg, value);
        }

        uint32_t pci_read([[maybe_unused]] const vm::pci::DeviceID dev, uint16_t reg, uint8_t size) {
            switch (size) {
                case 1: return space.data8[reg];
                case 2: return space.data16[reg / 2];
                case 4: return space.data32[reg / 4];
                default: PANIC("Unknown PCI Access size");
            }
                
            //print("q35::dram: Unhandled PCI read, reg: {:#x}, size: {:#x}\n", reg, (uint16_t)size);
        }

        void pam_update() {
            // TODO: Handle PAMs correctly
            //for(size_t i = 0; i < 13; i++) {
            //    auto pam = (space.data8[pam0 + div_ceil(i, 2)] >> ((!(i & 1)) * 4)) & 0b11;

                
            //}
        }

        pci::ConfigSpace space;
    };
} // namespace vm::q35::dram
