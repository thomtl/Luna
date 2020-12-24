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

        void pio_write(uint16_t port, uint32_t value, uint8_t size) {
            ASSERT(size == 1);

            if(port == reg_a) {
                auto a20 = (value >> 1) & 1;
                if(!a20)
                    PANIC("Guest attempted to clear A20 line");

                if(value != (1 << 1))
                    print("fast_20: Unhandled write to Port A: {:#x}\n", value);
            } else {
                print("fast_a20: Unhandled write to port {:#x}: {:#x}\n", port, value);
            }
        }

        uint32_t pio_read(uint16_t port, uint8_t size) {
            ASSERT(size == 1);
            if(port == reg_a)
                return (1 << 1); // Port A20 is always enabled
            else {
                print("fast_a20: Unhandled read from port {:#x}\n", port);

                return 0;    
            }
        }
    };
} // namespace vm::fast_a20
