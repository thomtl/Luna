#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

#include <Luna/cpu/lapic.hpp>

#include <Luna/misc/format.hpp>

#include <std/unordered_map.hpp>


namespace vm::irqs::lapic {
    struct Driver : public vm::AbstractMMIODriver {
        Driver(uint64_t base, uint8_t id): base{base}, id{id}, svr{0xFF} {}

        void register_mmio_driver(Vm* vm) {
            this->vm = vm;

            vm->mmio_map[0xFEE0'0000] = {this, 0x1000};
        }

        void mmio_write(uintptr_t addr, uint64_t value, uint8_t size) {
            using namespace ::lapic;
            if(addr == (base + regs::spurious)) {
                svr = value;

                auto spurious = svr & 0xFF;
                bool enable = (svr >> 8) & 1;
                print("lapic: Spurious IRQ: {:#x}, Enable: {}\n", (uint16_t)spurious, enable);
            } else if(addr == (base + regs::icr_low)) {
                icr = (icr >> 32) << 32;
                icr |= value;

                print("lapic: IPI: ICR: {:#x}\n", icr);
            } else if(addr == (base + regs::icr_high)) {
                icr = (icr << 32) >> 32;
                icr |= (value << 32);
            } else if(addr == (base + regs::lvt_lint0)) {
                lint0 = value;
            } else if(addr == (base + regs::lvt_lint1)) {
                lint1 = value;
            } else {
                print("lapic: Unhandled write to reg: {:#x} <- {:#x}, size {}\n", addr, value, (uint16_t)size);
            }
        }

        uint64_t mmio_read(uintptr_t addr, uint8_t size) {
            using namespace ::lapic;
            if(addr == (base + regs::id))
                return (id << 24);
            else if(addr == (base + regs::version))
                return (6 << 16) | 0x15; // 7 (6 + 1) LVT entries, Most recent LAPIC version, No EOI Broadcast suppress
            else if(addr == (base + regs::spurious))
                return svr;
            else if(addr == (base + regs::icr_low))
                return icr & 0xFFFF'FFFF;
            else if(addr == (base + regs::icr_high))
                return (icr >> 32) & 0xFFFF'FFFF;
            else if(addr == (base + regs::lvt_lint0))
                return lint0;
            else if(addr == (base + regs::lvt_lint1))
                return lint1;
            
            print("lapic: Unhandled read from reg: {:#x}, size {}\n", addr, (uint16_t)size);
            return 0;
        }

        private:
        uint64_t base;
        uint8_t id;

        uint32_t lint0, lint1;
        uint64_t icr;

        uint32_t svr;
        vm::Vm* vm;
    };
} // namespace vm::irqs::lapic
