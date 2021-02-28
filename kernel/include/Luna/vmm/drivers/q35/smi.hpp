#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

#include <Luna/misc/log.hpp>

namespace vm::q35::smi {
    // Technically called APM_CNT and APM_STS in the spec, but these names are more common
    constexpr uint16_t smi_cmd = 0xb2;
    constexpr uint16_t smi_sts = 0xb3;

    struct Driver : public vm::AbstractPIODriver {
        Driver(Vm* vm): vm{vm}, cmd{0}, sts{0}, smi_generation{false} {
            vm->pio_map[smi_cmd] = this;
            vm->pio_map[smi_sts] = this;
        }

        void enable_smi_generation(bool value) { smi_generation = value; }

        void pio_write(uint16_t port, uint32_t value, uint8_t size) {
            ASSERT(size == 1);

            if(port == smi_cmd) {
                cmd = value;

                if(smi_generation) {
                    //print("q35::smi: Raising SMI\n");

                    vm->cpus[0].enter_smm();
                }
            } else if(port == smi_sts) {
                sts = value;
            }
        }

        uint32_t pio_read(uint16_t port, uint8_t size) {
            ASSERT(size == 1);

            if(port == smi_cmd)
                return cmd;
            else if(port == smi_sts)
                return sts;
            
            PANIC("Unknown SMI Reg");
        }

        vm::Vm* vm;

        uint8_t cmd, sts;
        bool smi_generation;
    };
} // namespace vm::q35::smi
