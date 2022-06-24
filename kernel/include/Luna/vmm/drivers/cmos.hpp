#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

#include <Luna/misc/log.hpp>

namespace vm::cmos {
    constexpr uint16_t base = 0x70;
    constexpr uint8_t cmd = 0;
    constexpr uint8_t data = 1;

    constexpr uint8_t rtc_seconds = 0x0;
    constexpr uint8_t rtc_minutes = 0x2;
    constexpr uint8_t rtc_hours = 0x4;
    constexpr uint8_t rtc_day = 0x7;
    constexpr uint8_t rtc_month = 0x8;
    constexpr uint8_t rtc_year = 0x9;

    constexpr uint8_t rtc_century = 0x32;

    constexpr uint8_t cmos_extmem2_low = 0x34;
    constexpr uint8_t cmos_extmem2_high = 0x35;

    constexpr uint8_t cmos_bootflag1 = 0x38;
    constexpr uint8_t cmos_bootflag2 = 0x3d;

    constexpr uint8_t cmos_ap_count = 0x5f;

    constexpr uint8_t implemented_regs[] = {
        cmos_extmem2_low, cmos_extmem2_high, cmos_bootflag1, cmos_bootflag2, cmos_ap_count,
        rtc_seconds, rtc_minutes, rtc_hours, // Just ignore for now, should be implemented though
        rtc_day, rtc_month, rtc_year, rtc_century,
        0xF, // Reset status, used by BIOS

    };

    struct Driver final : public vm::AbstractPIODriver {
        Driver(Vm* vm) {
            vm->pio_map[base + cmd] = this;
            vm->pio_map[base + data] = this;

            memset(ram, 0, 128);
            ram[0xD] = 0x80; // CMOS Battery power good
        }

        void pio_write(uint16_t port, uint32_t value, [[maybe_unused]] uint8_t size) {
            if(port == (base + cmd)) {
                nmi = (value >> 7) & 1;
                address = value & ~0x80; // Clear off NMI mask
            } else if(port == (base + data)) {
                ram[address] = value;

                if(!reg_is_implemented(address))
                    print("cmos: Unhandled Write ram[{:#x}] = {:#x}\n", (uint16_t)address, (uint16_t)value);
            } else {
                print("cmos: Unhandled CMOS write: {:#x} <- {:#x}\n", port, value);
            }
        }

        uint32_t pio_read(uint16_t port, [[maybe_unused]] uint8_t size) {
            if(port == (base + cmd)) {
                return address | (nmi << 7);
            } else if(port == (base + data)) {
                auto ret = ram[address];

                if(!reg_is_implemented(address))
                    print("cmos: Unhandled Read ram[{:#x}] = {:#x}\n", (uint16_t)address, (uint16_t)ret);

                return ret;
            } else {
                print("cmos: Unhandled CMOS read: {:#x}\n", port);
                return 0;
            }
        }

        uint8_t read(uint8_t i) const { return ram[i]; }
        void write(uint8_t i, uint8_t v) { ram[i] = v; }

        private:
        bool reg_is_implemented(uint8_t reg) {
            for(auto i : implemented_regs)
                if(i == reg)
                    return true;
            return false;
        }

        uint8_t address;
        bool nmi;

        uint8_t ram[128];
    };
} // namespace vm::cmos
