#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

#include <Luna/misc/log.hpp>

#include <Luna/vmm/drivers/pci/pci.hpp>
#include <Luna/vmm/drivers/pci/ecam.hpp>

namespace vm::q35::dram {
    constexpr uint16_t cap_off = 0xE0;

    constexpr uint16_t pam0 = 0x90;
    constexpr uint16_t pam_size = 7;
    constexpr uint16_t n_pam = 13;

    constexpr uint8_t pam_readable = 0b01;
    constexpr uint8_t pam_writable = 0b10;

    constexpr uint16_t pciexbar = 0x60;
    constexpr uint16_t pciexbar_size = 0x8;

    constexpr uint64_t pciexbar_len_256 = 0b00;
    constexpr uint64_t pciexbar_len_128 = 0b01;
    constexpr uint64_t pciexbar_len_64 = 0b10;
    constexpr uint64_t pciexbar_enable = (1 << 0);

    constexpr uint16_t smram = 0x9D;
    constexpr uint16_t smram_global_enable = (1 << 3);
    constexpr uint16_t smram_lock = (1 << 4);
    constexpr uint16_t smram_closed = (1 << 5);
    constexpr uint16_t smram_open = (1 << 6);

    constexpr struct {
        uintptr_t base, limit;
    } pam_regions[] = {
        {.base = 0xF'0000, .limit = 0xF'FFFF}, // PAM0 hi

        {.base = 0xC'0000, .limit = 0xC'3FFF}, // PAM1 lo
        {.base = 0xC'4000, .limit = 0xC'7FFF}, //      hi
        {.base = 0xC'8000, .limit = 0xC'BFFF}, // PAM2 lo
        {.base = 0xC'C000, .limit = 0xC'FFFF}, //      hi

        {.base = 0xD'0000, .limit = 0xD'3FFF}, // PAM3 lo
        {.base = 0xD'4000, .limit = 0xD'7FFF}, //      hi
        {.base = 0xD'8000, .limit = 0xD'BFFF}, // PAM4 lo
        {.base = 0xD'C000, .limit = 0xD'FFFF}, //      hi

        {.base = 0xE'0000, .limit = 0xE'3FFF}, // PAM5 lo
        {.base = 0xE'4000, .limit = 0xE'7FFF}, //      hi
        {.base = 0xE'8000, .limit = 0xE'BFFF}, // PAM6 lo
        {.base = 0xE'C000, .limit = 0xE'FFFF}, //      hi
    };

    constexpr uint32_t c_smram_base = 0xA'0000;
    constexpr uint32_t c_smram_limit = 0xB'FFFF;

    struct Driver : public vm::pci::AbstractPCIDriver {
        Driver(vm::Vm* vm, pci::ecam::Driver* ecam): ecam{ecam}, vm{vm} {
            space.header.vendor_id = 0x8086;
            space.header.device_id = 0x29C0;

            space.header.command = (1 << 1);
            space.header.status = (1 << 4) | (1 << 7);

            space.header.revision = 2;

            space.header.class_id = 6;
            space.header.subclass = 0;
            space.header.prog_if = 0;

            space.header.capabilities = cap_off;

            space.header.subsystem_vendor_id = 0x1AF4;
            space.header.subsystem_device_id = 0x1100;

            space.data8[cap_off] = 0b1001; // Vendor dependent
            space.data8[cap_off + 1] = 0; // No next cap
            space.data8[cap_off + 2] = 0xB; // Length
            space.data8[cap_off + 3] = 1; // Low Nybble = version
            // Rest of the cap fields are 0

            space.data8[smram] = 0x2;
        }

        void register_pci_driver(vm::pci::HostBridge* bus) {
            bus->register_pci_driver(vm::pci::DeviceID{0, 0, 0, 0}, this); // Bus 0, Slot 0, Func 0
        }

        void pci_write([[maybe_unused]] const vm::pci::DeviceID dev, uint16_t reg, uint32_t value, uint8_t size) {
            auto do_write = [&] {
                switch (size) {
                    case 1: space.data8[reg] = value; break;
                    case 2: space.data16[reg / 2] = value; break;
                    case 4: space.data32[reg / 4] = value; break;
                    default: PANIC("Unknown PCI Access size");
                }
            };
            

            if(ranges_overlap(reg, size, 0, sizeof(pci::ConfigSpaceHeader)))
                pci_update(reg, size, value);
            else if(ranges_overlap(reg, size, pam0, pam_size)) {
                do_write();
                pam_update();
            } else if(ranges_overlap(reg, size, pciexbar, pciexbar_size)) {
                do_write();
                pciexbar_update();
            } else if(ranges_overlap(reg, size, smram, 1)) {
                do_write();
                smram_update();
            } else
                print("q35::dram: Unhandled PCI write, reg: {:#x}, value: {:#x}\n", reg, value);
        }

        uint32_t pci_read([[maybe_unused]] const vm::pci::DeviceID dev, uint16_t reg, uint8_t size) {
            uint32_t ret = 0;
            switch (size) {
                case 1: ret = space.data8[reg]; break;
                case 2: ret = space.data16[reg / 2]; break;
                case 4: ret = space.data32[reg / 4]; break;
                default: PANIC("Unknown PCI Access size");
            }

            if(ranges_overlap(reg, size, 0, sizeof(pci::ConfigSpaceHeader)))
                ; // Nothing special to do here
            else if(ranges_overlap(reg, size, pam0, pam_size))
                ; // Nothing special to do here
            else
                print("q35::dram: Unhandled PCI read, reg: {:#x}, size: {:#x}\n", reg, (uint16_t)size);

            return ret;
        }

        // TODO: Abstract this to common class
        void pci_update(uint16_t reg, uint8_t size, uint32_t value) {
            // TODO: This is horrible and broken and horrible
            auto handle_bar = [&](uint16_t bar) {
                if(reg != bar)
                    return false;
                
                ASSERT(size == 4); // Please don't tell me anyone does unaligned BAR r/w
                if(value == 0xFFFF'FFFF) // Do stupid size thing
                    space.data32[reg / 4] = 0; // We don't decode any bits
                else
                    space.data32[reg / 4] = value;

                return true;
            };

            if(handle_bar(0x10)) // BAR0
                return;
            if(handle_bar(0x14)) // BAR1
                return;
            if(handle_bar(0x18)) // BAR2
                return;
            if(handle_bar(0x1C)) // BAR3
                return;
            if(handle_bar(0x20)) // BAR4
                return;
            if(handle_bar(0x24)) // BAR5
                return;
            if(reg == 0x30) {
                space.data32[reg / 4] = 0; // No Option ROM
                return;
            }

            switch (size) {
                case 1: space.data8[reg] = value; break;
                case 2: space.data16[reg / 2] = value; break;
                case 4: space.data32[reg / 4] = value; break;
                default: PANIC("Unknown PCI Access size");
            }
        }

        void pciexbar_update() {
            uint64_t ecam_base = space.data32[pciexbar / 4] | ((uint64_t)space.data32[(pciexbar / 4) + 1] << 32);
            auto base = (ecam_base >> 26) << 26;
            auto length = (ecam_base >> 1) & 0b11;
            bool enabled = (ecam_base >> 0) & 0b1;

            size_t size = 0;
            uint8_t bus_end = 0;
            if(length == pciexbar_len_256) {
                size = 256; bus_end = 255;
            } else if(length == pciexbar_len_128) {
                size = 128; bus_end = 127;
            } else if(length == pciexbar_len_64) {
                size = 64; bus_end = 63;
            }

            //print("q35::dram: PCIe ECAM: {:#x}, Len: {}MiB, {}\n", base, size, enabled ? "Enabled" : "Disabled");

            size *= 1024 * 1024; // MiB -> Bytes

            pci::ecam::EcamConfig config{.base = base, .size = size, .bus_start = 0, .bus_end = bus_end, .enabled = enabled};
            ecam->update_region(config);
        }

        void pam_update() {
            for(size_t i = 0; i < 13; i++) {
                auto pam = (space.data8[pam0 + div_ceil(i, 2)] >> ((!(i & 1)) * 4)) & 0b11;

                if(pam != pam_cache[i]) {
                    if(i == 0) // Keep 0xF'0000 to 0x10'0000 writeable, because SeaBios tries to write to it even though it disables it
                        pam |= pam_writable;
                    //print("PAM{}, {:#x} -> {:#x}, {:#b}\n", i, pam_regions[i].base, pam_regions[i].limit, (uint16_t)pam);

                    for(size_t addr = pam_regions[i].base; addr < pam_regions[i].limit; addr += pmm::block_size)
                        vm->mm->protect(addr, paging::mapPagePresent | ((pam & pam_writable) ? paging::mapPageWrite : 0) | paging::mapPageExecute);

                    pam_cache[i] = pam;
                }
            }
        }

        void smram_update() {
            auto& v = space.data8[smram];

            v &= ~0b111;
            v |= 0b010; // Field is hardwired to this

            if((v & smram_lock) || smram_locked) {
                smram_locked = true;
                v |= smram_lock;
                return;
            }

            smram_enabled = (v & smram_global_enable) ? true : false;

            if(!smram_enabled)
                return;

            if((v & smram_open) && (v & smram_closed))
                PANIC("SMRAM Open and Closed at thesame time");

            bool new_state = false;
            if(v & smram_open)
                new_state = true;
            
            // TODO: SMRAM can be closed after lock bit is set
            if(v & smram_closed)
                PANIC("TODO: Implement SMRAM Closing"); // Code accesses references reference SMRAM, however Data accesses reference VGA VRAM

            if(new_state != smram_accessible) {
                uint64_t flags = 0;
                if(new_state)
                    flags |= paging::mapPagePresent | paging::mapPageWrite | paging::mapPageExecute;

                for(size_t addr = c_smram_base; addr < c_smram_limit; addr += pmm::block_size)
                    vm->mm->protect(addr, flags);

                smram_accessible = new_state;
            }
        }

        void smm_enter() {
            if(smram_accessible)
                return;
            
            for(size_t addr = c_smram_base; addr < c_smram_limit; addr += pmm::block_size)
                vm->mm->protect(addr, paging::mapPagePresent | paging::mapPageWrite | paging::mapPageExecute);
        }

        void smm_leave() {
            if(smram_accessible)
                return;

            for(size_t addr = c_smram_base; addr < c_smram_limit; addr += pmm::block_size)
                vm->mm->protect(addr, 0);
        }

        pci::ecam::Driver* ecam;
        pci::ConfigSpace space;

        uint8_t pam_cache[n_pam];
        bool smram_locked = false, smram_accessible = true, smram_enabled = false; // TODO: Maybe SMRAM shouldn't be enabled by default

        vm::Vm* vm;
    };
} // namespace vm::q35::dram
