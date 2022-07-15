#include <Luna/vmm/drivers/hpet.hpp>
#include <Luna/cpu/tsc.hpp>
#include <Luna/misc/log.hpp>

using namespace vm::hpet;

Driver::Driver(Vm* vm): vm{vm} {
    vm->mmio_map[base] = {this, 0x1000};

    config_val = 0;
    counter_val = 0;

    for(auto& comparator : comparators) {
        comparator.config &= ~comparator_config_clear_bits;
        comparator.config |= comparator_config_set_bits;
    }
}

void Driver::mmio_write(uintptr_t addr, uint64_t value, uint8_t size) {
    auto reg = addr - base;
            
    if(reg == config_reg && (size == 4 || size == 8)) {
        if(size == 8) {
            config_val = value;
        } else {
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
    } else {
        print("hpet: Unknown write {:#x} <- {:#x} with size {:#x}\n", reg, value, size);
    }
}

uint64_t Driver::mmio_read(uintptr_t addr, uint8_t size) {
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

uint64_t Driver::update_counter() {
    if(!counter_running)
        return counter_val;

    auto time = tsc::time_ns();
    counter_val += (time - last_update_time) / clk_period;

    last_update_time = time;

    return counter_val;
}

void Driver::reset_counter() {
    last_update_time = tsc::time_ns();
}

void Driver::update_comparator(uint32_t id, uint64_t new_config) {
    new_config &= ~comparator_config_clear_bits;
    new_config |= comparator_config_set_bits;

    ASSERT(!(new_config & (1 < 14))); // No FSB

    comparators[id].is_32bit = (new_config >> 8) & 1;
    comparators[id].periodic = (new_config >> 3) & 1;
    comparators[id].set_periodic_accumulator = (new_config >> 6) & 1;
    comparators[id].enable_irqs = (new_config >> 2) & 1;

    comparators[id].config = new_config;
}