#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>



namespace vm::gpu::vga {
    struct Driver : public vm::AbstractMMIODriver, public vm::AbstractPIODriver {
        Driver(Vm* vm) {
            vm->mmio_map[0xA'0000] = {this, 0x2'0000};
            vm->pio_map[0x3D4] = this;
            vm->pio_map[0x3D5] = this;
        }
        
        void mmio_write([[maybe_unused]] uintptr_t addr, [[maybe_unused]] uint64_t value, [[maybe_unused]] uint8_t size) {

        }

        uint64_t mmio_read([[maybe_unused]] uintptr_t addr, [[maybe_unused]] uint8_t size) { 
            return 0;
        }

        void pio_write(uint16_t port, uint32_t value, uint8_t size) {
            if(port == 0x3D4) {
                ASSERT(size == 1); // 2 byte writes are also allowed where the value to be written is the high byte

                index_3d4 = value;
            } else if(port == 0x3D5) {
                ASSERT(size == 1);

                if(index_3d4 == 0xE || index_3d4 == 0xF) // Cursor position
                    ;
                else
                    print("vga: Unknown write to timing reg {:#x} <- {:#x}\n", index_3d4, value);
            } else {
                print("vga: Unknown VGA PIO write: {:#x} <- {:#x} (size: {})\n", port, value, size);
            }
        }

        uint32_t pio_read(uint16_t port, uint8_t size) {
            print("vga: Unknown VGA PIO read: {:#x} (size: {})\n", port, size);
            return 0;
        }

        private:
        log::Logger* logger;

        uint8_t index_3d4;
    };
} // namespace vm::e9
