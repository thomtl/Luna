#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>



namespace vm::gpu::vga {
    struct Driver : public vm::AbstractMMIODriver {
        Driver(Vm* vm) {
            vm->mmio_map[0xA'0000] = {this, 0x2'0000};
        }
        
        void mmio_write([[maybe_unused]] uintptr_t addr, [[maybe_unused]] uint64_t value, [[maybe_unused]] uint8_t size) {

        }

        uint64_t mmio_read([[maybe_unused]] uintptr_t addr, [[maybe_unused]] uint8_t size) { 
            return 0;
        }

        private:
        log::Logger* logger;
    };
} // namespace vm::e9
