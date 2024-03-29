#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

#include <Luna/drivers/timers/timers.hpp>
#include <Luna/misc/log.hpp>

#include <Luna/vmm/drivers/pci/pci_driver.hpp>
#include <Luna/vmm/drivers/gpu/edid.hpp>

#include <Luna/vmm/drivers/ps2.hpp>

#include <Luna/gui/windows/fb_window.hpp>

namespace vm::gpu::bga {
    constexpr size_t dispi = 0x500;

    constexpr uint32_t lfb_size = 4 * 1024 * 1024;
    constexpr uint32_t mmio_size = 0x1000;

    constexpr size_t edid_size = 256;

    namespace regs {
        constexpr size_t id = dispi + (0 * 2);
        constexpr size_t xres = dispi + (1 * 2);
        constexpr size_t yres = dispi + (2 * 2);
        constexpr size_t bpp = dispi + (3 * 2);
        constexpr size_t enable = dispi + (4 * 2);
        constexpr size_t bank = dispi + (5 * 2);
        constexpr size_t vwidth = dispi + (6 * 2);
        constexpr size_t vheight = dispi + (7 * 2);
        constexpr size_t x_offset = dispi + (8 * 2);
        constexpr size_t y_offset = dispi + (9 * 2);
        constexpr size_t video_memory_64k = dispi + (0xa * 2);
    } // namespace regs

    constexpr size_t max_x = 800, max_y = 600;
    

    struct Driver final : public vm::AbstractMMIODriver, vm::pci::PCIDriver {
        Driver(vm::Vm* vm, pci::HostBridge* bridge, vfs::File* vgabios, uint8_t slot, vm::AbstractKeyboardListener* keyboard): PCIDriver{vm}, vm{vm}, keyboard{keyboard} {
            bridge->register_pci_driver(pci::DeviceID{0, 0, slot, 0}, this);
            pci_set_option_rom(vgabios);
            pci_init_bar(0, lfb_size, true);
            pci_init_bar(2, mmio_size, true);
            
            pci_space->header.vendor_id = 0x1234;
            pci_space->header.device_id = 0x1111;

            pci_space->header.class_id = 3;
            pci_space->header.subclass = 0;
            pci_space->header.prog_if = 0;

            this->edid = edid::generate_edid({.native_x = max_x, .native_y = max_y});

            fb = {(uint8_t*)hmm::alloc(lfb_size, 0x1000), lfb_size};
        }

        void register_mmio_driver(Vm* vm) { ASSERT(this->vm == vm); }

        void mmio_write(uintptr_t addr, uint64_t value, [[maybe_unused]] uint8_t size) {
            if(addr == (bar2 + 0x400)) {
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
            } else if(addr == bar2 + regs::bank) {
                ASSERT(value == 0);
            } else if(addr == bar2 + regs::vwidth) {
                ASSERT(value == mode.x); // TODO

                virtual_width = value;
                virtual_height = lfb_size / (value * (mode.bpp / 8));
            } else if(addr == bar2 + regs::vheight) {
                if(virtual_height != value)
                    PANIC("vbe: Virtual Height mismatch");
            } else if(addr == bar2 + regs::x_offset) {
                ASSERT(value == 0);
            } else if(addr == bar2 + regs::y_offset) {
                ASSERT(value == 0);
            } else if(addr == bar2 + regs::enable) {
                mode.enabled = value & 1;
                if(curr_mode.enabled == false && mode.enabled == true) {
                    window = new gui::FbWindow{{(int32_t)mode.x, (int32_t)mode.y}, fb.data(), "VM Screen", [](void* userptr, gui::KeyOp op, gui::KeyCodes code) { ((Driver*)userptr)->keyboard->handle_key_op(op, code); }, this};
                    gui::get_desktop().add_window(window);

                    
                    spawn([this] {
                        Promise<void> promise{};

                        constexpr size_t refresh_rate_hz = 50;
                        constexpr size_t refresh_rate_ms = 1000 / refresh_rate_hz;

                        timer::Timer timer{TimePoint::from_ms(refresh_rate_ms), true, [](void* promise) {
                            ((Promise<void>*)promise)->complete();
                        }, &promise};

                        timer.start();

                        while(1) {
                            promise.await();
                            promise.reset();
                            
                            window->update();
                        }
                    });

                    curr_mode = mode;
                } else { }
            } else {
                print("bga: Unhandled MMIO Write {:#x} <- {:#x}, bar2_addr: {:#x}\n", addr, value, addr - bar2);
            }
        }

        uint64_t mmio_read(uintptr_t addr, [[maybe_unused]] uint8_t size) {
            if(addr >= bar2 && addr < (bar2 + edid_size) && size == 1) {
                auto i = addr - bar2;
                if(i < 128) // We only have the base EDID block
                    return ((uint8_t*)&edid)[i];
                else
                    return 0;
            } else if(addr == bar2 + regs::video_memory_64k) {
                return (lfb_size / (64 * 1024));
            } else if(addr == bar2 + regs::id) {
                return 0xB0C5; // ID5
            } else
                print("bga: Unhandled MMIO Read {:#x}, bar2_reg: {:#x}\n", addr, addr - bar2);

            return 0;
        }

        uint32_t pci_handle_read(uint16_t reg, [[maybe_unused]] uint8_t size) {
            print("bga: Unhandled PCI Read from {:#x}\n", reg);
            return 0;
        }

        void pci_handle_write(uint16_t reg, uint32_t value, [[maybe_unused]] uint8_t size) {
            print("bga: Unhandled PCI Write {:#x} <- {:#x}\n", reg, value);
        }

        void pci_update_bars() {
            if(!(pci_space->header.command & (1 << 1))) // Make sure MMIO Decoding is on
                return;

            auto bar0 = (pci_space->header.bar[0] & ~0xF);
            auto bar2 = (pci_space->header.bar[2] & ~0xF);
            
            if(mmio_enabled) {
                vm->mmio_map[this->bar0] = {nullptr, 0};
                vm->mmio_map[this->bar2] = {nullptr, 0};
            }

            //vm->mmio_map[bar0] = {this, lfb_size};
            auto& kvmm = vmm::get_kernel_context();
            for(size_t i = 0; i < lfb_size; i += 0x1000)
                vm->mm->map(kvmm.get_phys((uintptr_t)fb.data() + i), bar0 + i, paging::mapPagePresent | paging::mapPageWrite);
            
            this->bar0 = bar0;

            vm->mmio_map[bar2] = {this, mmio_size};
            this->bar2 = bar2;
            mmio_enabled = true;
        }


        vm::Vm* vm;
        gui::FbWindow* window;
        std::span<uint8_t> fb;

        vm::AbstractKeyboardListener* keyboard;

        bool mmio_enabled = false;
        uint32_t bar0, bar2;

        gpu::edid::Edid edid;

        struct {
            size_t x, y, bpp;
            bool enabled;
        } curr_mode, mode;

        size_t virtual_width, virtual_height;
    };
} // namespace vm::gpu::bga
