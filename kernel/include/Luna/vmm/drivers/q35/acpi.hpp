#pragma once

#include <Luna/common.hpp>
#include <Luna/cpu/cpu.hpp>
#include <Luna/vmm/vm.hpp>

#include <Luna/misc/log.hpp>
#include <Luna/vmm/drivers/q35/smi.hpp>

#include <Luna/drivers/timers/timers.hpp>


namespace vm::q35::acpi {
    constexpr uint8_t size = 0x80;

    constexpr uint16_t pm_tmr = 0x8;

    constexpr uint16_t smi_en = 0x30;
    constexpr uint16_t glb_smi_en = (1 << 0);
    constexpr uint16_t apmc_en = (1 << 5);

    struct Driver : public vm::AbstractPIODriver {
        Driver(vm::Vm* vm, vm::q35::smi::Driver* smi_dev): vm{vm}, smi_dev{smi_dev}, start_ns{::timers::time_ns()} {}

        void update(bool enabled, uint16_t base) {
            if(this->enabled)
                for(uint8_t i = 0; i < size; i++)
                    vm->pio_map[this->base + i] = nullptr;

            this->enabled = enabled;
            this->base = base;
            for(uint8_t i = 0; i < size; i++)
                vm->pio_map[base + i] = this;
        }

        void pio_write(uint16_t port, uint32_t value, uint8_t size) {
            uint16_t reg = port - base;

            if(reg == smi_en) {
                ASSERT(size == 4);

                smi_enable = value;

                smi_dev->enable_smi_generation((value & smi_en) && (value & apmc_en));

                print("q35::acpi: SMI_EN = {:#x}\n", value);
            } else {
                print("q35::acpi: Write to unknown reg {:#x}, value: {:#x}\n", reg, value);
            }
        }

        uint32_t pio_read(uint16_t port, uint8_t size) {
            (void)size;
            uint16_t reg = port - base;
            uint32_t ret = 0;
            
            if(reg == smi_en)
                ret = smi_enable;
            else if(reg == pm_tmr) {
                auto time = timers::time_ns() - start_ns;

                auto ticks = (time * 3579545) / 1'000'000'000;

                ret = ticks & 0xFF'FFFF;
            } else
                print("q35::acpi: Read from unknown reg {:#x}\n", reg);
            
            return ret;
        }

        vm::Vm* vm;
        vm::q35::smi::Driver* smi_dev;

        bool enabled = false;
        uint16_t base = 0;






        uint64_t start_ns;

        uint32_t smi_enable;
    };
} // namespace vm::e9
