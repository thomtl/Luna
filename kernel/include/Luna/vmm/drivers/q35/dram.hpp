#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

#include <Luna/misc/log.hpp>

#include <Luna/vmm/drivers/pci/pci_driver.hpp>
#include <Luna/vmm/drivers/pci/ecam.hpp>

namespace vm::q35::dram {
    constexpr uint16_t cap_off = 0xE0;
    constexpr size_t cap_len = 0xB;

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

    struct Driver : vm::pci::PCIDriver {
        Driver(vm::Vm* vm, vm::pci::HostBridge* bus, pci::ecam::Driver* ecam): PCIDriver{vm}, ecam{ecam}, vm{vm} {
            bus->register_pci_driver(vm::pci::DeviceID{0, 0, 0, 0}, this); // Bus 0, Slot 0, Func 0

            pci_space->header.vendor_id = 0x8086;
            pci_space->header.device_id = 0x29C0;

            pci_space->header.command = (1 << 1);
            pci_space->header.status = (1 << 4) | (1 << 7);

            pci_space->header.revision = 2;

            pci_space->header.class_id = 6;
            pci_space->header.subclass = 0;
            pci_space->header.prog_if = 0;

            pci_space->header.capabilities = cap_off;

            pci_space->header.subsystem_vendor_id = 0x1AF4;
            pci_space->header.subsystem_device_id = 0x1100;

            pci_space->data8[cap_off] = 0b1001; // Vendor dependent
            pci_space->data8[cap_off + 1] = 0; // No next cap
            pci_space->data8[cap_off + 2] = cap_len; // Length
            pci_space->data8[cap_off + 3] = 1; // Low Nybble = version
            // Rest of the cap fields are 0

            pci_space->data8[smram] = 0x2;
        }

        void pci_handle_write(uint16_t reg, uint32_t value, uint8_t size) {
            auto do_write = [&] {
                switch (size) {
                    case 1: pci_space->data8[reg] = value; break;
                    case 2: pci_space->data16[reg / 2] = value; break;
                    case 4: pci_space->data32[reg / 4] = value; break;
                    default: PANIC("Unknown PCI Access size");
                }
            };
            
            if(ranges_overlap(reg, size, pam0, pam_size)) {
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

        uint32_t pci_handle_read(uint16_t reg, uint8_t size) {
            uint32_t ret = 0;
            switch (size) {
                case 1: ret = pci_space->data8[reg]; break;
                case 2: ret = pci_space->data16[reg / 2]; break;
                case 4: ret = pci_space->data32[reg / 4]; break;
                default: PANIC("Unknown PCI Access size");
            }

            if(ranges_overlap(reg, size, pam0, pam_size))
                ; // Nothing special to do here
            else if(ranges_overlap(reg, size, cap_off, cap_len))
                ;
            else
                print("q35::dram: Unhandled PCI read, reg: {:#x}, size: {:#x}\n", reg, (uint16_t)size);

            return ret;
        }

        void pci_update_bars() { }

        void pciexbar_update() {
            uint64_t ecam_base = pci_space->data32[pciexbar / 4] | ((uint64_t)pci_space->data32[(pciexbar / 4) + 1] << 32);
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
                auto pam = (pci_space->data8[pam0 + div_ceil(i, 2)] >> ((!(i & 1)) * 4)) & 0b11;

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
            auto& v = pci_space->data8[smram];

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

        uint8_t pam_cache[n_pam];
        bool smram_locked = false, smram_accessible = true, smram_enabled = false; // TODO: Maybe SMRAM shouldn't be enabled by default

        vm::Vm* vm;
    };
} // namespace vm::q35::dram
