#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

#include <Luna/misc/format.hpp>

namespace vm::cmos {
    constexpr uint16_t base = 0x70;
    constexpr uint8_t cmd = 0;
    constexpr uint8_t data = 1;

    struct Driver : public vm::AbstractDriver {
        void register_driver(Vm* vm) {
            vm->pio_map[base + cmd] = this;
            vm->pio_map[base + data] = this;

            memset(ram, 0, 128);
            ram[0xD] = 0x80; // CMOS Battery power good
        }

        void pio_write(uint16_t port, uint32_t value, [[maybe_unused]] uint8_t size) {
            if(port == (base + cmd)) {
                nmi = (value >> 7) & 1;
                address = value & ~0x80; // Clear off NMI mask
            } else if(port == (base + data)) {
                ram[address] = value;

                print("cmos: Write ram[{:#x}] = {:#x}\n", (uint16_t)address, (uint16_t)value);

                address = 0xD; // According to wiki.osdev.org/CMOS
            } else {
                print("cmos: Unhandled CMOS write: {:#x} <- {:#x}\n", port, value);
            }
        }

        uint32_t pio_read(uint16_t port, [[maybe_unused]] uint8_t size) {
            if(port == (base + cmd)) {
                return address | (nmi << 7);
            } else if(port == (base + data)) {
                auto ret = ram[address];

                print("cmos: Read ram[{:#x}] = {:#x}\n", (uint16_t)address, (uint16_t)ret);

                address = 0xD;

                return ret;
            } else {
                print("cmos: Unhandled CMOS read: {:#x}\n", port);
                return 0;
            }
        }

        private:
        uint8_t address;
        bool nmi;

        uint8_t ram[128];
    };
} // namespace vm::cmos
