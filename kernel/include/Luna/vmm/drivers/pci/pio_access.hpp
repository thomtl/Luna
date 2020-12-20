#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

#include <Luna/vmm/drivers/pci/pci.hpp>

#include <Luna/misc/format.hpp>

#include <std/unordered_map.hpp>

namespace vm::pci::pio_access {
    constexpr uint16_t default_base = 0xCF8;
    constexpr uint16_t config_address = 0;
    constexpr uint16_t config_data = 4;

    struct Driver : public vm::AbstractDriver, public vm::pci::AbstractPCIAccess {
        Driver(uint16_t base): base{base} {}

        void register_driver(Vm* vm) {
            vm->pio_map[base + config_address] = this;
            vm->pio_map[base + config_address + 1] = this;
            vm->pio_map[base + config_address + 2] = this;
            vm->pio_map[base + config_address + 3] = this;

            vm->pio_map[base + config_data] = this;
            vm->pio_map[base + config_data + 1] = this;
            vm->pio_map[base + config_data + 2] = this;
            vm->pio_map[base + config_data + 3] = this;
        }

        void register_pci_driver(DeviceID device, vm::pci::AbstractPCIDriver* driver) { drivers[device.raw] = driver; }

        void pio_write(uint16_t port, uint32_t value, uint8_t size) {
            if(port == (base + config_address) && size == 4) {
                addr.enable = (value >> 31) & 1;
                addr.dev.bus = (value >> 16) & 0xFF;
                addr.dev.slot = (value >> 11) & 0x1F;
                addr.dev.func = (value >> 8) & 0x7;
                addr.reg = value & 0xFC;
            } else if(port >= (base + config_data) && port <= (base + config_data + 4) && addr.enable) {
                auto off = port - (base + config_data);
                if(drivers.contains(addr.dev.raw)) {
                    drivers[addr.dev.raw]->pci_write(addr.dev, addr.reg + off, value, size);
                } else {
                    const auto dev = addr.dev;
                    print("pci: Unhandled PCI write, dev: {}:{}.{}, reg: {:#x}, size: {}, value: {:#x}\n", dev.bus, dev.slot, dev.func, addr.reg + off, (uint16_t)size, value);
                }
            } else {
                print("pci: Unhandled write to port {:#x}: {:#x}\n", port, value);
            }
        }

        uint32_t pio_read(uint16_t port, uint8_t size) {
            if(port >= (base + config_data) && port <= (base + config_data + 4) && addr.enable) {
                auto off = port - (base + config_data);
                if(drivers.contains(addr.dev.raw)) {
                    return drivers[addr.dev.raw]->pci_read(addr.dev, addr.reg + off, size);
                } else {
                    const auto dev = addr.dev;
                    print("pci: Unhandled PCI read, dev: {}:{}.{}, reg: {:#x}, size: {}\n", dev.bus, dev.slot, dev.func, addr.reg + off, (uint16_t)size);

                    return ~0; // PCI Returns all ones for non-existing devices
                }
            } else {
                print("pci: Unhandled read from port {:#x}\n", port);
            }

            return 0;
        }

        private:
        uint16_t base;
        std::unordered_map<uint16_t, vm::pci::AbstractPCIDriver*> drivers; 

        struct {
            bool enable;
            uint16_t reg;
            DeviceID dev;
        } addr;
    };
} // namespace vm::system_control
