
#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

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
        Driver(Vm* vm);

        void mmio_write(uintptr_t addr, uint64_t value, uint8_t size);
        uint64_t mmio_read(uintptr_t addr, uint8_t size);

        private:
        uint64_t update_counter();
        void reset_counter();
        void update_comparator(uint32_t id, uint64_t new_config);

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
