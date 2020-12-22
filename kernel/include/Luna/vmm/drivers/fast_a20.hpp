#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

#include <Luna/misc/format.hpp>

namespace vm::fast_a20 {
    constexpr uint16_t reg_a = 0x92;
    constexpr uint16_t reg_b = 0x61;

    struct Driver : public vm::AbstractPIODriver {
        void register_pio_driver(Vm* vm) {
            vm->pio_map[reg_a] = this;
            vm->pio_map[reg_b] = this;
        }

        void pio_write(uint16_t port, uint32_t value, [[maybe_unused]] uint8_t size) {
            print("fast_a20: Unhandled write to port {:#x}: {:#x}\n", port, value);
        }

        uint32_t pio_read(uint16_t port, [[maybe_unused]] uint8_t size) {
            print("fast_a20: Unhandled read from port {:#x}\n", port);

            return 0;
        }
    };
} // namespace vm::fast_a20
