#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

#include <Luna/misc/log.hpp>

namespace vm::pit {
    constexpr uint16_t channel2_status = 0x61;

    struct Driver : public vm::AbstractPIODriver {
        Driver(Vm* vm) {
            vm->pio_map[channel2_status] = this;
        }

        void pio_write(uint16_t port, uint32_t value, uint8_t size) {
            ASSERT(size == 1);
            
            print("pit: Unhandled write to port {:#x}: {:#x}\n", port, value);
        }

        uint32_t pio_read(uint16_t port, uint8_t size) {
            ASSERT(size == 1);
            if(port == channel2_status)
                return (1 << 5); // GRUB hack
            else {
                print("pit: Unhandled read from port {:#x}\n", port);

                return 0;    
            }
        }
    };
} // namespace vm::pit
