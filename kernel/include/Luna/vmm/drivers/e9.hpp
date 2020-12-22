#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

#include <Luna/misc/format.hpp>

namespace vm::e9 {
    struct Driver : public vm::AbstractPIODriver {
        void register_pio_driver(Vm* vm) {
            vm->pio_map[0xe9] = this;
        }

        void pio_write(uint16_t port, uint32_t value, uint8_t size) {
            ASSERT(port == 0xe9);
            ASSERT(size == 1);

            print("vm: Port e9: {}\n", (char)value);
        }

        uint32_t pio_read(uint16_t port, uint8_t size) {
            ASSERT(port == 0xe9);
            ASSERT(size == 1);

            return 0xE9;
        }
    };
} // namespace vm::e9
