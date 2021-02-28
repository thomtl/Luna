#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

#include <Luna/misc/log.hpp>

#include <Luna/vmm/drivers/pci/pci.hpp>
#include <Luna/vmm/drivers/gpu/edid.hpp>

#include <Luna/gui/fb.hpp>

namespace vm::gpu::bga {
    constexpr size_t dispi = 0x500;

    constexpr uint32_t lfb_size = 0x100000;
    constexpr uint32_t mmio_size = 0x1000;

    constexpr size_t edid_size = 256;

    namespace regs {
        constexpr size_t id = dispi + (0 * 2);
        constexpr size_t xres = dispi + (1 * 2);
        constexpr size_t yres = dispi + (2 * 2);
        constexpr size_t bpp = dispi + (3 * 2);
        constexpr size_t enable = dispi + (4 * 2);
    } // namespace regs

    constexpr size_t max_x = 512, max_y = 512;
    

    struct Driver : public vm::pci::AbstractPCIDriver, public vm::AbstractMMIODriver {
        Driver(vm::Vm* vm, pci::HostBridge* bridge, uint8_t slot): vm{vm} {
            bridge->register_pci_driver(pci::DeviceID{0, 0, slot, 0}, this);
            
            space.header.vendor_id = 0x1234;
            space.header.device_id = 0x1111;

            space.header.class_id = 3;
            space.header.subclass = 0;
            space.header.prog_if = 0;

            this->edid = edid::generate_edid({.native_x = max_x, .native_y = max_y});
        }

        void register_mmio_driver(Vm* vm) { ASSERT(this->vm == vm); }
        void register_pci_driver([[maybe_unused]] vm::pci::HostBridge* bus) { }

        void pci_write([[maybe_unused]] const vm::pci::DeviceID dev, uint16_t reg, uint32_t value, uint8_t size) {
            /*auto do_write = [&] {
                switch (size) {
                    case 1: space.data8[reg] = value; break;
                    case 2: space.data16[reg / 2] = value; break;
                    case 4: space.data32[reg / 4] = value; break;
                    default: PANIC("Unknown PCI Access size");
                }
            };*/
            
            if(ranges_overlap(reg, size, 0, sizeof(pci::ConfigSpaceHeader)))
                pci_update(reg, size, value);
            else
                print("bga: Unhandled PCI write, reg: {:#x}, value: {:#x}\n", reg, value);
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
            else
                print("bga: Unhandled PCI read, reg: {:#x}, size: {:#x}\n", reg, (uint16_t)size);

            return ret;
        }


        void mmio_write(uintptr_t addr, uint64_t value, [[maybe_unused]] uint8_t size) {
            if(addr >= bar0 && addr < (bar0 + lfb_size)) {
                auto off = addr - bar0;
                if(off <= (curr_mode.x * curr_mode.y * (curr_mode.bpp / 8))) {
                    window->get_fb()[off / 4] = value;
                }
            } else if(addr == (bar2 + 0x400)) {
                // Some kind of sync register?
            } else if(addr == bar2 + regs::xres) {
                ASSERT(value <= max_x);
                mode.x = value;
            } else if(addr == bar2 + regs::yres) {
                ASSERT(value <= max_y);
                mode.y = value;
            } else if(addr == bar2 + regs::bpp) {
                ASSERT(value == 32);
                mode.bpp = value;
            } else if(addr == bar2 + regs::enable) {
                mode.enabled = value & 1;
                if(curr_mode.enabled == false && mode.enabled == true) {
                    window = new gui::FbWindow{{(int32_t)mode.x, (int32_t)mode.y}, "VM Screen"};

                    gui::get_desktop().add_window(window);
                    gui::get_desktop().update();

                    curr_mode = mode;
                } else {
                    PANIC("TODO");
                }
            } else {
                print("bga: Unhandled MMIO Write {:#x} <- {:#x}\n", addr, value);
            }
        }

        uint64_t mmio_read(uintptr_t addr, [[maybe_unused]] uint8_t size) {
            if(addr >= bar0 && addr < (bar0 + lfb_size)) {
                auto off = addr - bar0;
                if(off <= (curr_mode.x * curr_mode.y * (curr_mode.bpp / 8))) {
                    return window->get_fb()[off / 4];
                }
            } else if(addr >= bar2 && addr < (bar2 + edid_size) && size == 1) {
                auto i = addr - bar2;
                if(i < 128) // We only have the base EDID block
                    return ((uint8_t*)&edid)[i];
                else
                    return 0;
            } else if(addr == bar2 + regs::id)
                return 0xB0C5; // ID5
            else
                print("bga: Unhandled MMIO Read {:#x}\n", addr);

            return 0;
        }

        // TODO: Abstract this to common class
        void pci_update(uint16_t reg, uint8_t size, uint32_t value) {
            // TODO: This is horrible and broken and horrible
            auto handle_bar = [&](uint16_t bar) {
                if(reg != bar)
                    return false;
                
                ASSERT(size == 4); // Please don't tell me anyone does unaligned BAR r/w
                if(value == 0xFFFF'FFFF) { // Do stupid size thing
                    if(bar == 0x10)
                        space.data32[reg / 4] = ~(lfb_size - 1);
                    else if(bar == 0x18)
                        space.data32[reg / 4] = ~(mmio_size - 1);
                    else
                        space.data32[reg / 4] = 0; // We don't decode any bits
                } else {
                    space.data32[reg / 4] = value;

                    update_bar();
                }

                return true;
            };

            if(reg == 4) {
                auto old = space.header.command;
                space.header.command = value;

                if((old & (1 << 1)) != (value & (1 << 1)))
                    update_bar();
            }

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
                if((value & 0xFFFF'F800) == 0xFFFF'F800)
                    space.data32[reg / 4] = ~(align_up(option_rom->get_size(), 0x1000) - 1) | 1;
                else {
                    space.data32[reg / 4] = value;
                    update_option_rom();
                }
                return;
            }

            switch (size) {
                case 1: space.data8[reg] = value; break;
                case 2: space.data16[reg / 2] = value; break;
                case 4: space.data32[reg / 4] = value; break;
                default: PANIC("Unknown PCI Access size");
            }
        }

        void set_option_rom(vfs::File* file) { option_rom = file; }

        void update_option_rom() {
            auto gpa = space.header.expansion_rom_base & ~1;
            auto size = align_up(option_rom->get_size(), 0x1000);

            if(!option_rom_on && space.header.expansion_rom_base & 1) {
                for(size_t i = 0; i < size; i += 0x1000) {
                    auto hpa = pmm::alloc_block();
                    ASSERT(hpa);

                    option_rom->read(i, 0x1000, (uint8_t*)(hpa + phys_mem_map));

                    vm->mm->map(hpa, gpa + i, paging::mapPagePresent);
                }

                option_rom_on = true;
                return;
            } else if(option_rom_on && !(space.header.expansion_rom_base & 1)) {
                for(size_t i = 0; i < size; i += 0x1000) {
                    auto hpa = vm->mm->unmap(gpa + i);
                    pmm::free_block(hpa);
                }
            } else if(!option_rom_on && !(space.header.expansion_rom_base & 1)) {
                // Its off and its staying off so there's nothing to do yet
            } else {
                PANIC("TODO");
            }
        }

        void update_bar() {
            if(!(space.header.command & (1 << 1)))
                return;

            auto bar0 = (space.header.bar[0] & ~0xF);
            auto bar2 = (space.header.bar[2] & ~0xF);
            
            if(mmio_enabled) {
                vm->mmio_map[this->bar0] = {nullptr, 0};
                vm->mmio_map[this->bar2] = {nullptr, 0};
            }

            vm->mmio_map[bar0] = {this, lfb_size};
            this->bar0 = bar0;

            vm->mmio_map[bar2] = {this, mmio_size};
            this->bar2 = bar2;
            mmio_enabled = true;
        }

        pci::ConfigSpace space;
        vm::Vm* vm;
        vfs::File* option_rom;
        gui::FbWindow* window;
        bool option_rom_on = false;

        bool mmio_enabled = false;
        uint32_t bar0, bar2;

        gpu::edid::Edid edid;

        struct {
            size_t x, y, bpp;
            bool enabled;
        } curr_mode, mode;
    };
} // namespace vm::gpu::bga
