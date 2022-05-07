#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>



namespace vm::irqs::ioapic {
    constexpr uint64_t ioregsel = 0;
    constexpr uint64_t iowin = 0x10;

    constexpr uint8_t ioapicid = 0;
    constexpr uint8_t ioapicver = 1;
    constexpr uint8_t ioapicarb = 2;
    constexpr uint8_t ioredtbl_start = 0x10;

    constexpr uint8_t ioapic_version = 0x20; // Most recent version
    constexpr uint8_t n_redirection_entries = 0x17;

    struct Driver : public vm::AbstractMMIODriver {
        Driver(Vm* vm, uint32_t apic_id, uint64_t base): vm{vm}, apic_id{apic_id}, base{base} {
            vm->mmio_map[base] = {this, 0x1000};
        }

        void mmio_write(uintptr_t addr, uint64_t value, uint8_t size) {
            ASSERT(size == 4);

            if(addr == (base + ioregsel))
                cur_index = value & 0xFF;
            else if(addr == (base + iowin))
                ioapic_write(cur_index, value);
            else
                print("ioapic: Unknown write to register {:#x} of size {:#x} with value {:#x}\n", addr - base, size, value);
        }

        uint64_t mmio_read(uintptr_t addr, uint8_t size) {
            ASSERT(size ==  4);

            if(addr == (base + ioregsel))
                return cur_index;
            else if(addr == (base + iowin))
                return ioapic_read(cur_index);
            else
                print("ioapic: Unknown read from register {:#x} of size {:#x}\n", addr - base, size);

            return 0;
        }

        private:
        void ioapic_write(uint8_t index, uint32_t value) {
            print("ioapic: Write to unknown IOAPIC register {:#x} <- {:#x}\n", index, value);
        }

        uint32_t ioapic_read(uint8_t index) {
            if(index == ioapicid)
                return ((apic_id & 0xFF) << 24);
            else if(index == ioapicver)
                return ioapic_version | (n_redirection_entries << 16);
            else if(index == ioapicarb)
                return ((apic_id & 0xFF) << 24); // TODO
            else {
                print("ioapic: Read from unknown IOAPIC register: {:#x}\n", index);
                return 0;
            }

        }

        vm::Vm* vm;
        uint32_t apic_id;
        uint64_t base;

        uint8_t cur_index;
    };
} // namespace vm::irqs::ioapic
