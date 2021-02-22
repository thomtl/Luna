
#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

#include <Luna/misc/log.hpp>


namespace vm::hpet {
    constexpr uintptr_t base = 0xFED0'0000;

    constexpr size_t clk_period = 10; // In ns
    constexpr size_t fs_per_ns = 100'0000;

    constexpr uintptr_t cap = 0;
    constexpr uintptr_t clk = 4;

    constexpr uint64_t cap_val = (0x8086 << 16) | (1 << 15) | (1 << 13) | (2 << 8) | 1; // LegacyReplacement Capable, 64bit timer, 3 comparators, Rev1
    constexpr uint64_t clk_val = clk_period * fs_per_ns;

    struct Driver : public vm::AbstractMMIODriver {
        void register_mmio_driver(Vm* vm) {
            this->vm = vm;

            vm->mmio_map[base] = {this, 0x1000};
        }

        void mmio_write(uintptr_t addr, uint64_t value, [[maybe_unused]] uint8_t size) {
            auto reg = addr - base;

            print("hpet: Unknown write {:#x} <- {:#x}\n", reg, value);
        }

        uint64_t mmio_read(uintptr_t addr, [[maybe_unused]] uint8_t size) {
            auto reg = addr - base;

            if(reg == cap && size == 4)
                return cap_val;
            else if(reg == cap && size == 8)
                return (clk_val << 32) | cap_val;
            else if(reg == clk && size == 4)
                return clk_val;
            else
                print("hpet: Unknown read from {:#x}\n", reg);
            
            return 0;
        }

        private:
        vm::Vm* vm;
    };
} // namespace vm::hpet
