#pragma once

#include <Luna/common.hpp>
#include <Luna/cpu/paging.hpp>
#include <Luna/vmm/vm.hpp>
#include <Luna/vmm/drivers/pci/pci.hpp>
#include <Luna/fs/vfs.hpp>

namespace vm::pci {
    struct PCIDriver : public AbstractPCIDriver {
        PCIDriver(Vm* vm): vm{vm} {}

        virtual ~PCIDriver() {}

        virtual uint32_t pci_handle_read(uint16_t reg, uint8_t size) = 0;
        virtual void pci_handle_write(uint16_t reg, uint32_t value, uint8_t size) = 0;
        virtual void pci_update_bars() = 0;

        // API Functions
        void pci_set_option_rom(vfs::File* file) { option_rom_file = file; }
        void pci_init_bar(uint8_t i, size_t size, bool is_mmio, bool is64 = false, bool is_prefetchable = false) {
            pci_bars[i] = {.is_mmio = is_mmio, .is64 = is64, .is_prefetchable = is_prefetchable, .is64_high_size = false, .size = (size & 0xFFFF'FFFF)};

            pci_space.header.bar[i] = (is_prefetchable ? (1 << 3) : 0) | (is64 ? (2 << 1) : 0) | (is_mmio ? 0 : 1); // This ofcourse doesn't work after the vm has started

            if(is64) {
                ASSERT(i <= 5); // You can't have a 64bit BAR as the last one, because then there isn't an upper one

                pci_bars[i + 1] = {.is_mmio = false, .is64 = false, .is_prefetchable = false, .is64_high_size = true, .size = ((size >> 32) & 0xFFFF'FFFF)};
                pci_space.header.bar[i + 1] = 0;
            }
        }

        ConfigSpace pci_space;
        protected:
        // Handlers
        void pci_write([[maybe_unused]] const vm::pci::DeviceID dev, uint16_t reg, uint32_t value, uint8_t size) {            
            if(ranges_overlap(reg, size, 0, sizeof(pci::ConfigSpaceHeader)))
                pci_update(reg, size, value);
            else // Delegate to real driver
                pci_handle_write(reg, value, size);
        }

        uint32_t pci_read([[maybe_unused]] const vm::pci::DeviceID dev, uint16_t reg, uint8_t size) {
            uint32_t ret = 0;
            switch (size) {
                case 1: ret = pci_space.data8[reg]; break;
                case 2: ret = pci_space.data16[reg / 2]; break;
                case 4: ret = pci_space.data32[reg / 4]; break;
                default: PANIC("Unknown PCI Access size");
            }

            if(ranges_overlap(reg, size, 0, sizeof(pci::ConfigSpaceHeader)))
                ; // Nothing special to do here
            else
                ret = pci_handle_read(reg, size);
            
            return ret;
        }

        void pci_update(uint16_t reg, uint8_t size, uint32_t value) {
            auto handle_bar = [&](uint16_t i) {
                auto bar = 0x10 + 4 * i;
                if(reg != bar)
                    return false;
                
                ASSERT(size == 4); // Please don't tell me anyone does unaligned BAR r/w
                if(value == 0xFFFF'FFFF) { // Do stupid size thing
                    if(pci_bars[i].size)
                        pci_space.header.bar[i] = ~((pci_bars[i].size & 0xFFFF'FFFF) - 1);
                    else
                        pci_space.header.bar[i] = 0;
                } else {
                    pci_space.header.bar[i] = value;

                    pci_update_bars();
                }

                return true;
            };

            if(reg == 4) { // PCI Command
                auto old = pci_space.header.command;
                pci_space.header.command = value;

                if((old & 0x3) != (value & 0x3)) // If the new IO or MMIO decoding state is different update bars too
                    pci_update_bars();
            }

            if(handle_bar(0)) // BAR0
                return;
            if(handle_bar(1)) // BAR1
                return;
            if(handle_bar(2)) // BAR2
                return;
            if(handle_bar(3)) // BAR3
                return;
            if(handle_bar(4)) // BAR4
                return;
            if(handle_bar(5)) // BAR5
                return;
            
            if(reg == 0x30) { // Option rom
                if(option_rom_file) {
                    if((value & 0xFFFF'F800) == 0xFFFF'F800) {
                        pci_space.header.expansion_rom_base = ~(align_up(option_rom_file->get_size(), 0x1000) - 1) | 1;
                    } else {
                        pci_space.header.expansion_rom_base = value;
                        update_option_rom();
                    }
                } else {
                    pci_space.header.expansion_rom_base = 0; // No option rom implemented
                }
                
                return;
            }

            switch (size) {
                case 1: pci_space.data8[reg] = value; break;
                case 2: pci_space.data16[reg / 2] = value; break;
                case 4: pci_space.data32[reg / 4] = value; break;
                default: PANIC("Unknown PCI Access size");
            }
        }

        void update_option_rom() {
            auto gpa = pci_space.header.expansion_rom_base & ~1;
            auto size = align_up(option_rom_file->get_size(), 0x1000);

            if(!option_rom_state && pci_space.header.expansion_rom_base & 1) { // Off -> On
                if(!(pci_space.header.command & (1 << 1))) // Memory Space Decoding has to be on for Expansion ROMs to be decoded
                    return;

                for(size_t i = 0; i < size; i += 0x1000) {
                    auto hpa = pmm::alloc_block();
                    ASSERT(hpa);

                    option_rom_file->read(i, 0x1000, (uint8_t*)(hpa + phys_mem_map));

                    vm->mm->map(hpa, gpa + i, paging::mapPagePresent);
                }

                option_rom_state = true;
                return;
            } else if(option_rom_state && !(pci_space.header.expansion_rom_base & 1)) { // On -> Off
                for(size_t i = 0; i < size; i += 0x1000) {
                    auto hpa = vm->mm->unmap(gpa + i);
                    pmm::free_block(hpa);
                }

                option_rom_state = false;
            } else if(!option_rom_state && !(pci_space.header.expansion_rom_base & 1)) {
                // Its off and its staying off so there's nothing to do yet
            } else {
                PANIC("TODO");
            }
        }

        vm::Vm* vm;

        vfs::File* option_rom_file = nullptr;
        bool option_rom_state = false;

        struct {
            bool is_mmio, is64, is_prefetchable, is64_high_size;
            size_t size;
        } pci_bars[6];
    };
} // namespace vm::pci
