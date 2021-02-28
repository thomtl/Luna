#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

#include <Luna/misc/log.hpp>

// https://github.com/qemu/qemu/blob/master/hw/acpi/pcihp.c
// https://github.com/qemu/qemu/blob/master/docs/specs/acpi_pci_hotplug.txt
namespace vm::pci::hotplug {
    constexpr uint16_t base = 0xAE00;
    constexpr uint16_t size = 0x10;

    struct Driver : public vm::AbstractPIODriver {
        Driver(Vm* vm) {
            for(size_t i = 0; i < size; i++)
                vm->pio_map[base + i] = this;
        }

        void pio_write(uint16_t port, uint32_t value, [[maybe_unused]] uint8_t size) {
            print("pci::hotplug: Unhandled Write: {:#x} <- {:#x}\n", port, value);
        }

        uint32_t pio_read(uint16_t port, [[maybe_unused]] uint8_t size) {
            print("pci::hotplug: Unhandled Read: {:#x}\n", port);

            return 0;
        }
    };
} // namespace vm::pci::hotplug
