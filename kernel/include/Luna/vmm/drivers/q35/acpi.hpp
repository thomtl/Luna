#pragma once

#include <Luna/common.hpp>
#include <Luna/cpu/cpu.hpp>
#include <Luna/vmm/vm.hpp>

#include <Luna/misc/log.hpp>
#include <Luna/vmm/drivers/q35/smi.hpp>

#include <Luna/drivers/timers/timers.hpp>


namespace vm::q35::acpi {
    constexpr uint8_t size = 0x80;

    constexpr uint16_t pm1_status = 0x0;

    constexpr uint16_t pm1_enable = 0x2;

    constexpr uint16_t pm1_control = 0x4;
    constexpr uint16_t sci_en = (1 << 0);
    constexpr uint16_t gbl_rls = (1 << 2);

    // Make sure these stay in sync with the values from seabios
    constexpr uint8_t acpi_enable_cmd = 0x2;
    constexpr uint8_t acpi_disable_cmd = 0x3;

    constexpr uint16_t pm_tmr = 0x8;

    constexpr uint16_t gpe0_sts = 0x20;
    constexpr uint16_t gpe0_en = 0x28;

    constexpr uint16_t smi_en = 0x30;
    constexpr uint16_t glb_smi_en = (1 << 0);
    constexpr uint16_t apmc_en = (1 << 5);

    struct Driver final : public vm::AbstractPIODriver {
        Driver(vm::Vm* vm, vm::q35::smi::Driver* smi_dev): vm{vm}, smi_dev{smi_dev}, start_ns{::timers::time_ns()} {
            smi_dev->register_smi_cmd_callback([](smi::Driver* smi, void* self_ptr) {
                auto& self = *(Driver*)self_ptr;

                switch(smi->cmd) {
                    case acpi_enable_cmd:
                        self.pm1_control_val |= sci_en;
                        break;
                    case acpi_disable_cmd:
                        PANIC("TODO: Implement ACPI disable"); // Should just be &= ~sci_en right?
                        break;
                    default:
                        if(self.generate_smis) {
                            //print("q35::smi: Raising SMI\n");
                            self.vm->cpus[0].enter_smm();
                        }
                        break;
                }
            }, this);
        }

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

            if(reg == pm1_enable) {
                ASSERT(size == 2);
                pm1_enable_val = value;
            } else if(reg == smi_en) {
                ASSERT(size == 4);
                smi_enable = value;

                generate_smis = (value & smi_en) && (value & apmc_en);

                //print("q35::acpi: SMI_EN = {:#x}\n", value);
            } else if(reg >= gpe0_en && reg < (gpe0_en + 8)) {
                ASSERT(size == 1);
                auto i = reg - gpe0_en;

                gpe0_en_val &= ~(0xFFull << (i * 8));
                gpe0_en_val |= ((uint64_t)value << (i * 8));
            } else {
                print("q35::acpi: Write to unknown reg {:#x}, value: {:#x}\n", reg, value);
            }
        }

        uint32_t pio_read(uint16_t port, uint8_t size) {
            uint16_t reg = port - base;
            uint32_t ret = 0;
            
            if(reg == pm1_enable) {
                ASSERT(size == 2);
                ret = pm1_enable_val;
            } else if(reg == smi_en) {
                ASSERT(size == 4);
                ret = smi_enable;
            } else if(reg == pm1_control) {
                ret = pm1_control_val;
            } else if(reg == pm_tmr) {
                ASSERT(size == 4);
                auto time = timers::time_ns() - start_ns;

                auto ticks = (time * 3579545) / 1'000'000'000;

                ret = ticks & 0xFF'FFFF;
            } else if(reg >= gpe0_en && reg < (gpe0_en + 8)) {
                ASSERT(size == 1);
                auto i = reg - gpe0_en;

                return (gpe0_en_val >> (i * 8)) & 0xFF;
            } else
                print("q35::acpi: Read from unknown reg {:#x}\n", reg);
            
            return ret;
        }

        vm::Vm* vm;
        vm::q35::smi::Driver* smi_dev;

        bool enabled = false, generate_smis = false;
        uint16_t base = 0;

        uint64_t start_ns;

        uint32_t smi_enable, pm1_control_val;
        uint16_t pm1_status_val, pm1_enable_val;

        uint64_t gpe0_sts_val, gpe0_en_val;
    };
} // namespace vm::e9
