
#pragma once

#include <Luna/common.hpp>
#include <Luna/cpu/tsc.hpp>

#include <Luna/vmm/vm.hpp>

#include <Luna/misc/log.hpp>

namespace vm::hpet {
    constexpr uintptr_t base = 0xFED0'0000;

    constexpr size_t clk_period = 10; // In ns
    constexpr size_t fs_per_ns = 100'0000;
    constexpr uint64_t n_comparators = 3;

    constexpr uintptr_t cap_reg = 0;
    constexpr uintptr_t clk_reg = 4;
    constexpr uintptr_t config_reg = 0x10;
    constexpr uintptr_t counter_reg = 0xF0;

    constexpr uintptr_t comparator_config_reg = 0;
    constexpr uintptr_t comparator_value_reg = 8;
    constexpr uintptr_t comparator_fsb_irq_route_reg = 0x10;

    
    constexpr uint64_t cap_val = (0x8086 << 16) | (1 << 15) | (1 << 13) | ((n_comparators - 1) << 8) | 1; // LegacyReplacement Capable, 64bit timer, 3 comparators, Rev1
    constexpr uint64_t clk_val = clk_period * fs_per_ns;

    constexpr uint64_t comparator_config_clear_bits = (1 << 15); // No FSB IRQs support
    constexpr uint64_t comparator_config_set_bits = (1 << 4) | (1 << 5); // Periodic support, 64bit timer

    struct Driver final : public vm::AbstractMMIODriver {
        Driver(Vm* vm): vm{vm} {
            vm->mmio_map[base] = {this, 0x1000};

            config_val = 0;
            counter_val = 0;

            for(auto& comparator : comparators) {
                comparator.config &= ~comparator_config_clear_bits;
                comparator.config |= comparator_config_set_bits;
            }
        }

        void mmio_write(uintptr_t addr, uint64_t value, [[maybe_unused]] uint8_t size) {
            auto reg = addr - base;
            
            if(reg == config_reg && (size == 4 || size == 8)) {
                if(size == 8)
                    config_val = value;
                else {
                    config_val &= ~0xFFFF'FFFF;
                    config_val |= value;
                }

                if((config_val & 1) && !counter_running)
                    reset_counter();
                
                counter_running = config_val & 1;
                legacy_replacement_mapping = (config_val >> 1) & 1;
            } else if(reg == counter_reg && size == 4) {
                ASSERT(!counter_running);

                counter_val &= ~0xFFFF'FFFF;
                counter_val |= value;
            } else if(reg == (counter_reg + 4) && size == 4) {
                counter_val &= ~0xFFFF'FFFF'0000'0000;
                counter_val |= (uint64_t)value << 32;
            } else if(reg >= 0x100) { // Comparator
                const auto [id, comparator_reg] = decode_comparator_reg(reg);
                ASSERT(id < n_comparators);

                if(comparator_reg == comparator_config_reg && size == 4) {
                    auto new_cfg = comparators[id].config;
                    new_cfg &= ~0xFFFF'FFFF;
                    new_cfg |= value;

                    update_comparator(id, new_cfg);
                } else if(comparator_reg == (comparator_config_reg + 4) && size == 4) {
                    auto new_cfg = comparators[id].config;
                    new_cfg &= ~0xFFFF'FFFF'0000'0000;
                    new_cfg |= (value << 32);

                    update_comparator(id, new_cfg);
                } else
                    print("hpet: Unknown write to comparator {} reg: {:#x} <- {:#x} with size {:#x}\n", id, comparator_reg, value, size);
            } else  {
                print("hpet: Unknown write {:#x} <- {:#x} with size {:#x}\n", reg, value, size);
            }
        }

        uint64_t mmio_read(uintptr_t addr, [[maybe_unused]] uint8_t size) {
            auto reg = addr - base;

            if(reg == cap_reg && size == 4) {
                return cap_val;
            } else if(reg == cap_reg && size == 8) {
                return (clk_val << 32) | cap_val;
            } else if(reg == clk_reg && size == 4) {
                return clk_val;
            } else if(reg == config_reg && size == 4) {
                return (config_val & 0xFFFF'FFFF);
            } else if(reg == (config_reg + 4) && size == 4) {
                return (config_val >> 32) & 0xFFFF'FFFF;
            } else if(reg == config_reg && size == 8) {
                return config_val;
            } else if(reg == counter_reg && size == 4) {
                return update_counter();
            } else if(reg >= 0x100) { // Comparators
                const auto [id, comparator_reg] = decode_comparator_reg(reg);
                ASSERT(id < n_comparators);

                if(comparator_reg == comparator_config_reg && size == 4)
                    return comparators[id].config & 0xFFFF'FFFF;
                else if(comparator_reg == (comparator_config_reg + 4) && size == 4)
                    return (comparators[id].config >> 32) & 0xFFFF'FFFF;
                else
                    print("hpet: Unknown read from comparator {} reg: {:#x} with size {:#x}\n", id, comparator_reg, size);
            } else {
                print("hpet: Unknown read from {:#x} with size {:#x}\n", reg, size);
            }
            
            return 0;
        }

        private:
        uint64_t update_counter() {
            if(!counter_running)
                return counter_val;

            auto time = tsc::time_ns();

            counter_val += (time - last_update_time) / clk_period;

            last_update_time = time;

            return counter_val;
        }

        void reset_counter() {
            last_update_time = tsc::time_ns();
        }

        void update_comparator(uint32_t id, uint64_t new_config) {
            new_config &= ~comparator_config_clear_bits;
            new_config |= comparator_config_set_bits;

            ASSERT(!(new_config & (1 < 14))); // No FSB

            comparators[id].is_32bit = (new_config >> 8) & 1;
            comparators[id].periodic = (new_config >> 3) & 1;
            comparators[id].set_periodic_accumulator = (new_config >> 6) & 1;
            comparators[id].enable_irqs = (new_config >> 2) & 1;

            comparators[id].config = new_config;
        }

        constexpr std::pair<uint32_t, uint32_t> decode_comparator_reg(uint32_t reg) {
            reg -= 0x100;
            return {(reg & ~0x1F) / 0x20, reg & 0x1F};
        }

        vm::Vm* vm;
        uint64_t config_val;

        bool counter_running, legacy_replacement_mapping;
        uint64_t counter_val, last_update_time;

        struct {
            uint64_t config, value;

            bool is_32bit, periodic, set_periodic_accumulator, enable_irqs;
        } comparators[n_comparators];
    };
} // namespace vm::hpet
