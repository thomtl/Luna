#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

namespace vm::io_delay {
    struct Driver : public vm::AbstractPIODriver {
        Driver(Vm* vm) {
            vm->pio_map[0xED] = this; // 0xED and 0x80 are commonly used as short io delays, but thats only needed on real hw
            vm->pio_map[0x80] = this;
        }
        
        void pio_write(uint16_t port, [[maybe_unused]] uint32_t value, [[maybe_unused]] uint8_t size) {
            ASSERT(port == 0xED || port == 0x80);
        }

        uint32_t pio_read(uint16_t port, [[maybe_unused]] uint8_t size) {
            ASSERT(port == 0xED || port == 0x80);
            
            return 0;
        }
    };
} // namespace vm::io_delay
