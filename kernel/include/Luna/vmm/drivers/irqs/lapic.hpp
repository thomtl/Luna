#pragma once

#include <Luna/common.hpp>

#include <Luna/cpu/lapic.hpp>

#include <Luna/misc/log.hpp>

#include <std/unordered_map.hpp>

#include <Luna/vmm/drivers.hpp>


namespace vm::irqs::lapic {
    // LAPIC is a bit weird and not a normal MMIO driver
    struct Driver : public vm::AbstractMMIODriver {
        Driver(uint8_t id): id{id}, svr{0xFF} {}

        void register_mmio_driver([[maybe_unused]] Vm* vm) {}

        void update_apicbase(uint64_t value) {
            base = value & ~0xFFF;
            ASSERT(value & (1 << 11)); // Assert xAPIC is enabled
            ASSERT(!(value & (1 << 10))); // Assert x2APIC is disabled
        }

        void mmio_write(uintptr_t addr, uint64_t value, uint8_t size) {
            using namespace ::lapic;
            if(addr == (base + regs::spurious)) {
                svr = value;

                spurious_vector = svr & 0xFF;
                enabled = (svr >> 8) & 1;
            } else if(addr == (base + regs::icr_low)) {
                icr = (icr >> 32) << 32;
                icr |= value;

                print("lapic: TODO IPI: ICR: {:#x}\n", icr);
            } else if(addr == (base + regs::icr_high)) {
                icr = (icr << 32) >> 32;
                icr |= (value << 32);
            } else if(addr == (base + regs::lvt_lint0)) {
                lint0 = value;
            } else if(addr == (base + regs::lvt_lint1)) {
                lint1 = value;
            } else if(addr == (base + regs::ldr)) {
                ldr = value;

                logical_id = (ldr >> 24) & 0xFF;
            } else if(addr == (base + regs::dfr)) {
                dfr = value;

                ASSERT((dfr & 0x0FFF'FFFF) == 0x0FFF'FFFF);
                destination_mode = static_cast<regs::DestinationModes>((dfr >> 28) & 0xF);
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
            else if(addr == (base + regs::ldr))
                return ldr;
            else if(addr == (base + regs::dfr))
                return dfr;
            else
                print("lapic: Unhandled read from reg: {:#x}, size {}\n", addr, (uint16_t)size);
            
            return 0;
        }

        private:
        uint64_t base;

        uint8_t spurious_vector;
        uint8_t id, logical_id;

        bool enabled;

        uint32_t lint0, lint1;
        uint64_t icr, dfr, ldr;
        ::lapic::regs::DestinationModes destination_mode;

        uint32_t svr;
        vm::Vm* vm;
    };
} // namespace vm::irqs::lapic
