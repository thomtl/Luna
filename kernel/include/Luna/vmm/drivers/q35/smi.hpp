#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

#include <Luna/misc/log.hpp>

namespace vm::q35::smi {
    // Technically called APM_CNT and APM_STS in the spec, but these names are more common
    constexpr uint16_t smi_cmd = 0xb2;
    constexpr uint16_t smi_sts = 0xb3;

    struct Driver final : public vm::AbstractPIODriver {
        Driver(Vm* vm): vm{vm}, cmd{0}, sts{0}, smi_callback{nullptr}, smi_userptr{nullptr} {
            vm->pio_map[smi_cmd] = this;
            vm->pio_map[smi_sts] = this;
        }

        void register_smi_cmd_callback(void (*f)(Driver*, void*), void* userptr) { 
            smi_callback = f;
            smi_userptr = userptr;
        }

        void pio_write(uint16_t port, uint32_t value, uint8_t size) {
            ASSERT(size == 1);

            if(port == smi_cmd) {
                cmd = value;

                if(smi_callback)
                    smi_callback(this, smi_userptr);
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

        void (*smi_callback)(Driver*, void*);
        void* smi_userptr;
    };
} // namespace vm::q35::smi
