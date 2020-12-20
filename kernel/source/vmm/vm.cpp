#include <Luna/vmm/vm.hpp>

#include <Luna/misc/format.hpp>

#include <Luna/cpu/intel/vmx.hpp>
#include <Luna/cpu/amd/svm.hpp>

void vm::init() {
    if(vmx::is_supported()) {
        get_cpu().cpu.vm.vendor = CpuVendor::Intel;

        vmx::init();
    } else if(svm::is_supported()) {
        get_cpu().cpu.vm.vendor = CpuVendor::AMD;

        svm::init();
    } else
        PANIC("Unknown virtualization vendor");
}

vm::Vm::Vm() {
    uint64_t cr0_constraint = 0, cr4_constraint = 0, efer_constraint = 0;
    switch (get_cpu().cpu.vm.vendor) {
        case CpuVendor::Intel:
            vm = new vmx::Vm{};

            cr0_constraint = vmx::get_cr0_constraint();
            cr4_constraint = vmx::get_cr4_constraint();
            break;
        case CpuVendor::AMD:
            vm = new svm::Vm{};

            cr0_constraint = svm::get_cr0_constraint();
            efer_constraint = svm::get_efer_constraint();
            break;
        default:
            PANIC("Unknown virtualization vendor");
    }

    vm::RegisterState regs{};

    constexpr uint16_t code_access = 0b11 | (1 << 4) | (1 << 7) | (1 << 13);
    constexpr uint16_t data_access = 0b11 | (1 << 4) | (1 << 7);
    constexpr uint16_t ldtr_access = 0b10 | (1 << 7);
    constexpr uint16_t tr_access = 0b11 | (1 << 7);

    regs.cs = {.selector = 0xF000, .base = 0xFFFF0000, .limit = 0xFFFF, .attrib = code_access};

    regs.ds = {.selector = 0, .base = 0, .limit = 0xFFFF, .attrib = data_access};
    regs.es = {.selector = 0, .base = 0, .limit = 0xFFFF, .attrib = data_access};
    regs.ss = {.selector = 0, .base = 0, .limit = 0xFFFF, .attrib = data_access};
    regs.fs = {.selector = 0, .base = 0, .limit = 0xFFFF, .attrib = data_access};
    regs.gs = {.selector = 0, .base = 0, .limit = 0xFFFF, .attrib = data_access};

    regs.tr = {.selector = 0, .base = 0, .limit = 0xFFFF, .attrib = tr_access};
    regs.ldtr = {.selector = 0, .base = 0, .limit = 0xFFFF, .attrib = ldtr_access};

    regs.idtr = {.base = 0, .limit = 0xFFFF};
    regs.gdtr = {.base = 0, .limit = 0xFFFF};

    regs.dr6 = 0xFFFF0FF0;
    regs.dr7 = 0x400;
    regs.rsp = 0;

    regs.rip = 0xFFF0;
    regs.rflags = (1 << 1);

    regs.cr0 = (cr0_constraint & ~((1 << 0) | (1 << 31))); // Clear PE and PG;
    regs.cr4 = cr4_constraint;

    regs.cr3 = 0;
    regs.efer = efer_constraint;

    vm->set_regs(regs);

    auto& simd = vm->get_guest_simd_context();
    simd.data()->fcw = 0x40;
    simd.data()->mxcsr = 0x1F80;
}
        
void vm::Vm::get_regs(vm::RegisterState& regs) const { vm->get_regs(regs); }
void vm::Vm::set_regs(const vm::RegisterState& regs) { vm->set_regs(regs); }

void vm::Vm::map(uintptr_t hpa, uintptr_t gpa, uint64_t flags) { vm->map(hpa, gpa, flags); }
bool vm::Vm::run() {
    while(true) {
        vm::RegisterState regs{};
        vm::VmExit exit{};

        if(!vm->run(exit))
            return false;

        switch (exit.reason) {
        case VmExit::Reason::Vmcall: {
            get_regs(regs);

            uint32_t op = regs.rax & 0xFFFF'FFFF;
            if(op == 0) {
                print("vm: Guest requested exit\n", op);
                return true;
            } else if(op == 1) {
                uint16_t size = regs.rcx & 0xFFFF;
                uint16_t disk_index = (regs.rcx >> 16) & 0x7FFF;
                bool address_size = (regs.rcx & (1 << 31)) ? true : false;

                uintptr_t dest = regs.rdi & (address_size ? 0xFFFF'FFFF'FFFF'FFFF : 0xFFFF'FFFF);
                uintptr_t off = regs.rbx & (address_size ? 0xFFFF'FFFF'FFFF'FFFF : 0xFFFF'FFFF);

                print("vm: Guest requested disk read, Disk: {}, Size: {}, Dest {:#x}, Off: {}, AS: {}\n", disk_index, size, dest, off, address_size);
                if(disk_index >= disks.size()) {
                    regs.rflags |= 1; // Set CF
                    set_regs(regs);
                } else {
                    auto& disk = disks[disk_index];
                    if(!disk) {
                        regs.rflags |= 1; // Set CF
                        set_regs(regs);
                    } else {
                        auto* buffer = new uint8_t[size];
                        if(disk->read(off, size, buffer) != size) {
                            regs.rflags |= 1; // Set CF
                            set_regs(regs);
                        } else {
                            // TODO: Get rid of these assertions
                            auto page_off = dest & 0xFFF;
                            ASSERT(size <= (0x1000 - page_off));

                            auto hpa = vm->get_phys(dest);
                            ASSERT(hpa); // Assert that the page is actually mapped
                            auto* host_buf = (uint8_t*)(hpa + phys_mem_map);

                            memcpy(host_buf, buffer, size);
                        }

                        delete[] buffer;
                    }
                }
            } else {
                print("vm: Unknown VMMCALL opcode {:#x}\n", op);
                return false;
            }
            break;
        }
        case VmExit::Reason::MMUViolation:
            get_regs(regs);
            print("vm: MMU Violation\n");
            print("    gRIP: {:#x}, gPA: {:#x}\n", regs.cs.base + regs.rip, exit.mmu.gpa);
            print("    Access: {:s}{:s}{:s}, {:s}\n", exit.mmu.access.r ? "R" : "", exit.mmu.access.w ? "W" : "", exit.mmu.access.x ? "X" : "", exit.mmu.access.user ? "User" : "Supervisor");
            print("    Page: {:s}{:s}{:s}, {:s}\n", exit.mmu.page.r ? "R" : "", exit.mmu.page.w ? "W" : "", exit.mmu.page.x ? "X" : "", exit.mmu.page.user ? "User" : "Supervisor");
            if(exit.mmu.reserved_bits_set)
                print("    Reserved bits set\n");
            return false;

        case VmExit::Reason::PIO: {
            get_regs(regs); 

            ASSERT(!exit.pio.rep); // TODO
            ASSERT(!exit.pio.string);

            auto reg_clear = [&]<typename T>(T& value) {
                switch(exit.pio.size) {
                    case 1: value &= 0xFF; break;
                    case 2: value &= 0xFFFF; break;
                    case 4: value &= 0xFFFF'FFFF; break;
                    default: PANIC("Unknown PIO Size");
                }
            };

            if(!pio_map.contains(exit.pio.port)) {
                print("vm: Unhandled PIO Access to port {:#x}\n", exit.pio.port);
                return false;
            }

            auto* driver = pio_map[exit.pio.port];

            if(exit.pio.write) {
                auto value = regs.rax;
                reg_clear(value);

                driver->pio_write(exit.pio.port, value, exit.pio.size);
            } else {
                auto value = driver->pio_read(exit.pio.port, exit.pio.size);

                switch(exit.pio.size) {
                    case 1: regs.rax &= ~0xFF; break;
                    case 2: regs.rax &= ~0xFFFF; break;
                    case 4: regs.rax &= ~0xFFFF'FFFF; break;
                    default: PANIC("Unknown PIO Size");
                }
                reg_clear(value);

                regs.rax |= value;

                set_regs(regs);
            }

            break;
        }

        case VmExit::Reason::CPUID: {
            get_regs(regs);

            auto write_low32 = [&](uint64_t& reg, uint32_t val) { reg &= ~0xFFFF'FFFF; reg |= val; };

            auto leaf = regs.rax & 0xFFFF'FFFF;
            auto subleaf = regs.rcx & 0xFFFF'FFFF;

            if(leaf == 0x4000'0000) {
                uint32_t luna_sig = 0x616E754C; // Luna in ASCII

                write_low32(regs.rax, 0);
                write_low32(regs.rbx, luna_sig);
                write_low32(regs.rcx, luna_sig);
                write_low32(regs.rdx, luna_sig);
            } else {
                print("vm: Unhandled CPUID: {:#x}:{}\n", leaf, subleaf);
            }

            set_regs(regs);
            break;
        }
        
        default:
            print("vm: Exit due to {:s}\n", exit.reason_to_string(exit.reason));
            if(exit.instruction_len != 0) {
                print("         Opcode: ");
                for(size_t i = 0; i < exit.instruction_len; i++)
                    print("{:#x} ", (uint64_t)exit.instruction[i]);
                print("\n");
            }
            break;
        }
    } 
    return true;
}