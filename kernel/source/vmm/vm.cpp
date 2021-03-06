#include <Luna/vmm/vm.hpp>

#include <Luna/misc/log.hpp>

#include <Luna/cpu/intel/vmx.hpp>
#include <Luna/cpu/amd/svm.hpp>

#include <Luna/vmm/emulate.hpp>

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

vm::VCPU::VCPU(vm::Vm* vm, uint8_t id): vm{vm}, lapic{id} {
    switch (get_cpu().cpu.vm.vendor) {
        case CpuVendor::Intel:
            vcpu = new vmx::Vm{vm->mm, this};

            cr0_constraint = vmx::get_cr0_constraint();
            cr4_constraint = vmx::get_cr4_constraint();
            break;
        case CpuVendor::AMD:
            vcpu = new svm::Vm{vm->mm, this};

            cr0_constraint = svm::get_cr0_constraint();
            efer_constraint = svm::get_efer_constraint();
            break;
        default:
            PANIC("Unknown virtualization vendor");
    }

    vm::RegisterState regs{};

    regs.cs = {.selector = 0xF000, .base = 0xFFFF'0000, .limit = 0xFFFF, .attrib = {.type = 0b11, .s = 1, .present = 1}};

    regs.ds = {.selector = 0, .base = 0, .limit = 0xFFFF, .attrib = {.type = 0b11, .s = 1, .present = 1}};
    regs.es = {.selector = 0, .base = 0, .limit = 0xFFFF, .attrib = {.type = 0b11, .s = 1, .present = 1}};
    regs.ss = {.selector = 0, .base = 0, .limit = 0xFFFF, .attrib = {.type = 0b11, .s = 1, .present = 1}};
    regs.fs = {.selector = 0, .base = 0, .limit = 0xFFFF, .attrib = {.type = 0b11, .s = 1, .present = 1}};
    regs.gs = {.selector = 0, .base = 0, .limit = 0xFFFF, .attrib = {.type = 0b11, .s = 1, .present = 1}};

    regs.ldtr = {.selector = 0, .base = 0, .limit = 0xFFFF, .attrib = {.type = 2, .present = 1}};
    regs.tr = {.selector = 0, .base = 0, .limit = 0xFFFF, .attrib = {.type = 3, .present = 1}};

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

    vcpu->set_regs(regs, VmRegs::General | VmRegs::Segment | VmRegs::Control);

    auto& simd = vcpu->get_guest_simd_context();
    simd.data()->fcw = 0x40;
    simd.data()->mxcsr = 0x1F80;

    // MSR init
    apicbase = 0xFEE0'0000 | (1 << 11) | ((id == 0) << 8); // xAPIC enable, If id == 0 set BSP bit too
    lapic.update_apicbase(apicbase);

    smbase = 0x3'0000;
}
        
void vm::VCPU::get_regs(vm::RegisterState& regs, uint64_t flags) const { vcpu->get_regs(regs, flags); }
void vm::VCPU::set_regs(const vm::RegisterState& regs, uint64_t flags) { vcpu->set_regs(regs, flags); }
void vm::VCPU::set(VmCap cap, bool value) { vcpu->set(cap, value); }
void vm::VCPU::set(VmCap cap, void (*fn)(void*), void* userptr) { 
    if(cap == VmCap::SMMEntryCallback) {
        smm_entry_callback = fn;
        smm_entry_userptr = userptr;
    } else if(cap == VmCap::SMMLeaveCallback) {
        smm_leave_callback = fn;
        smm_leave_userptr = userptr;
    } else
        PANIC("Unknown cap");
}

bool vm::VCPU::run() {
    while(true) {
        vm::RegisterState regs{};
        vm::VmExit exit{};

        if(!vcpu->run(exit))
            return false;

        switch (exit.reason) {
        case VmExit::Reason::Vmcall: { // For now a VMMCALL is just an exit
            return true;
        }

        case VmExit::Reason::MMUViolation: {
            get_regs(regs);

            auto grip = regs.cs.base + regs.rip;

            auto emulate_mmio = [&](AbstractMMIODriver* driver, uintptr_t base, size_t size) {
                uint8_t instruction[max_x86_instruction_size];
                dma_read(grip, {instruction, 15});

                vm::emulate::emulate_instruction(this, {base, size}, instruction, regs, driver);
                set_regs(regs);
            };

            if((exit.mmu.gpa & ~0xFFF) == (apicbase & ~0xFFF)) {
                emulate_mmio(&lapic, apicbase & ~0xFFF, 0x1000);
                goto did_mmio;
            }
            
            for(const auto [base, driver] : vm->mmio_map) {
                if(exit.mmu.gpa >= base && exit.mmu.gpa <= (base + driver.second))  {
                    // Access is in an MMIO region
                    emulate_mmio(driver.first, base, driver.second);
                    goto did_mmio;
                }
            }

            // No MMIO region, so a page violation
            print("vm: MMU Violation\n");
            print("    gRIP: {:#x}, gPA: {:#x}\n", grip, exit.mmu.gpa);
            print("    Access: {:s}{:s}{:s}, {:s}\n", exit.mmu.access.r ? "R" : "", exit.mmu.access.w ? "W" : "", exit.mmu.access.x ? "X" : "", exit.mmu.access.user ? "User" : "Supervisor");
            if(exit.mmu.page.present)
                print("    Page: {:s}{:s}{:s}, {:s}\n", exit.mmu.page.r ? "R" : "", exit.mmu.page.w ? "W" : "", exit.mmu.page.x ? "X" : "", exit.mmu.page.user ? "User" : "Supervisor");
            else
                print("    Page: Not present\n");
            if(exit.mmu.reserved_bits_set)
                print("    Reserved bits set\n");
            return false;

            did_mmio:
            break;
        }

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

            if(!vm->pio_map.contains(exit.pio.port)) {
                print("vcpu: Unhandled PIO Access to port {:#x}\n", exit.pio.port);

                if(!exit.pio.write) {
                    switch(exit.pio.size) {
                        case 1: regs.rax &= ~0xFF; break;
                        case 2: regs.rax &= ~0xFFFF; break;
                        case 4: regs.rax &= ~0xFFFF'FFFF; break;
                        default: PANIC("Unknown PIO Size");
                    }
                }
                
                set_regs(regs);
                break;
            }

            auto* driver = vm->pio_map[exit.pio.port];

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

            constexpr uint32_t luna_sig = 0x616E754C; // Luna in ASCII
            
            auto passthrough = [&]() {
                uint32_t a, b, c, d;
                ASSERT(cpu::cpuid(leaf, subleaf, a, b, c, d));

                write_low32(regs.rax, a);
                write_low32(regs.rbx, b);
                write_low32(regs.rcx, c);
                write_low32(regs.rdx, d);
            };

            auto os_support_bit = [&](uint64_t& reg, uint8_t cr4_bit, uint8_t bit) {
                reg &= ~(1 << bit);

                bool os = (regs.cr4 >> cr4_bit) & 1;
                reg |= (os << bit);
            };

            if(leaf == 0) {
                passthrough();
            } else if(leaf == 1) {
                passthrough();

                regs.rcx |= (1 << 31); // Set Hypervisor Present bit

                os_support_bit(regs.rdx, 9, 24);
                os_support_bit(regs.rcx, 18, 27); // Only set OSXSAVE bit if actually enabled by OS
            } else if(leaf == 0x4000'0000) {
                write_low32(regs.rax, 0);
                write_low32(regs.rbx, luna_sig);
                write_low32(regs.rcx, luna_sig);
                write_low32(regs.rdx, luna_sig);
            } else if(leaf == 0x8000'0000) {
                passthrough();
            } else if(leaf == 0x8000'0001) {
                passthrough();
                os_support_bit(regs.rdx, 9, 24);
            } else if(leaf == 0x8000'0008) {
                passthrough(); // TODO: Do we want this to be passthrough?
                write_low32(regs.rcx, 0); // Clear out core info
            } else {
                print("vcpu: Unhandled CPUID: {:#x}:{}\n", leaf, subleaf);
            }

            set_regs(regs);
            break;
        }

        case VmExit::Reason::MSR: {
            get_regs(regs);
            auto index = regs.rcx & 0xFFFF'FFFF;
            auto value = (regs.rax & 0xFFFF'FFFF) | (regs.rdx << 32);

            auto write_low32 = [&](uint64_t& reg, uint32_t val) { reg &= ~0xFFFF'FFFF; reg |= val; };

            if(index == msr::ia32_tsc) {
                if(exit.msr.write)
                    tsc = value;
                else
                    value = tsc;
            } else if(index == msr::ia32_mtrr_cap) {
                if(exit.msr.write)
                    vcpu->inject_int(AbstractVm::InjectType::Exception, 13, true, 0); // Inject #GP(0)

                value = (1 << 10) | (1 << 8) | 8; // WC valid, Fixed MTRRs valid, 8 Variable MTRRs
            } else if(index == msr::ia32_apic_base) {
                if(exit.msr.write) {
                    apicbase = value;
                    lapic.update_apicbase(apicbase);
                } else {
                    value = apicbase;
                }
            } else if(index >= 0x200 && index <= 0x2FF) {
                update_mtrr(exit.msr.write, index, value);
            } else if(index == msr::ia32_efer) {
                if(exit.msr.write)
                    regs.efer = value | efer_constraint;
                else
                    value = regs.efer;
            } else {
                if(exit.msr.write) {
                    print("vcpu: Unhandled wrmsr({:#x}, {:#x})\n", index, value);
                } else {
                    print("vcpu: Unhandled rdmsr({:#x})\n", index);
                    value = 0;
                }
            }
            
            if(!exit.msr.write) {
                write_low32(regs.rax, value & 0xFFFF'FFFF);
                write_low32(regs.rdx, value >> 32);
            }

            set_regs(regs);
            break;
        }

        case VmExit::Reason::RSM: {
            if(is_in_smm)
                handle_rsm();
            else
                vcpu->inject_int(vm::AbstractVm::InjectType::Exception, 6); // Inject a UD
            break;
        }

        case VmExit::Reason::Hlt: {
            print("vm: HLT\n");
            return false;
        }

        case VmExit::Reason::CrMov: {
            get_regs(regs);

            uint64_t value = 0;

            if(exit.cr.write)
                value = vm::emulate::read_r64(regs, (vm::emulate::r64)exit.cr.gpr, 8);
            
            if(exit.cr.cr == 0) {
                if(exit.cr.write)
                    regs.cr0 = value;
                else
                    PANIC("TODO");
            } else if(exit.cr.cr == 3) {
                if(exit.cr.write) {
                    regs.cr3 = value;
                    guest_tlb.invalidate();
                } else
                    value = regs.cr3;
            } else {
                PANIC("TODO");
            }

            if(!exit.cr.write)
                vm::emulate::write_r64(regs, (vm::emulate::r64)exit.cr.gpr, value, 8);

            set_regs(regs);
            break;
        }
        
        default:
            print("vcpu: Exit due to {:s}\n", exit.reason_to_string(exit.reason));
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

void vm::VCPU::update_mtrr(bool write, uint32_t index, uint64_t& value) {
    auto update = [this](){
        // We can mostly just ignore MTRRs and whatever guests want for paging, as we force WB
        // However when VT-d doesn't support snooping, it's needed to mark pages as UC, when passing through devices
        // AMD-Vi always supports snooping, and this is always forced on, so no such thing is needed

        // Debug code for printing MTRRs
        /*print("vm::mtrr: Update, Enable: {}, Fixed Enable: {}, Default Type: {}\n", mtrr.enable, mtrr.fixed_enable, (uint16_t)mtrr.default_type);

        for(size_t i = 0; i < 8; i++) {
            auto var = mtrr.var[i];

            if(!(var.mask & 0x800)) // Valid
                continue;

            var.mask &= ~0x800;

            auto physical_bits = 36;
            {
                uint32_t a, b, c, d;
                if(cpu::cpuid(0x8000'0008, a, b, c, d))
                    physical_bits = a & 0xFF;
            }
            auto size = -var.mask & ((1ull << physical_bits) - 1);
            auto start = var.base & ~0xFFF;
            auto type = var.base & 0xFF;

            print("vm::mtrr: Var{}: {:#x} -> {:#x} => Type: {}\n", i, start, start + size - 1, type);
        }

        if(mtrr.fixed_enable) {
            print("vm::mtrr: fix64K_00000: {:#x}\n", mtrr.fix[0]);

            print("vm::mtrr: fix16K_80000: {:#x}\n", mtrr.fix[1]);
            print("vm::mtrr: fix16K_A0000: {:#x}\n", mtrr.fix[2]);

            print("vm::mtrr: fix4K_C0000: {:#x}\n", mtrr.fix[3]);
            print("vm::mtrr: fix4K_C8000: {:#x}\n", mtrr.fix[4]);
            print("vm::mtrr: fix4K_D0000: {:#x}\n", mtrr.fix[5]);
            print("vm::mtrr: fix4K_D8000: {:#x}\n", mtrr.fix[6]);
            print("vm::mtrr: fix4K_E0000: {:#x}\n", mtrr.fix[7]);
            print("vm::mtrr: fix4K_E8000: {:#x}\n", mtrr.fix[8]);
            print("vm::mtrr: fix4K_F0000: {:#x}\n", mtrr.fix[9]);
            print("vm::mtrr: fix4K_F8000: {:#x}\n", mtrr.fix[10]);
        }*/
    };

    if(index == msr::ia32_mtrr_def_type) {
        if(write) {
            mtrr.cmd = value;

            mtrr.enable = (value >> 11) & 1;
            mtrr.fixed_enable = (value >> 10) & 1;
            mtrr.default_type = value & 0xFF;

            update();
        } else {
            value = mtrr.cmd;
        }
    } else if(index >= msr::ia32_mtrr_physbase0 && index <= msr::ia32_mtrr_physmask7) {
        size_t i = ((index - msr::ia32_mtrr_physbase0) & ~1) / 2;
        size_t mask = index & 1;

        if(write) {
            if(mask)
                mtrr.var[i].mask = value;
            else
                mtrr.var[i].base = value;

            update();
        } else {
            if(mask)
                value = mtrr.var[i].mask;
            else
                value = mtrr.var[i].base;
        }
    } else if(index == msr::ia32_mtrr_fix64K_00000) {
        if(write) {
            mtrr.fix[0] = value;
            update();
        } else {
            value = mtrr.fix[0];
        }
    } else if(index == msr::ia32_mtrr_fix16K_80000 || index == msr::ia32_mtrr_fix16K_A0000) {
        size_t i = (index - msr::ia32_mtrr_fix16K_80000) + 1;
        if(write) {
            mtrr.fix[i] = value;
            update();
        } else {
            value = mtrr.fix[i];
        }
    } else if(index >= msr::ia32_mtrr_fix4K_C0000 && index <= msr::ia32_mtrr_fix4K_F8000) {
        size_t i = (index - msr::ia32_mtrr_fix4K_C0000) + 3;
        if(write) {
            mtrr.fix[i] = value;
            update();
        } else {
            value = mtrr.fix[i];
        }
    } else {
        print("vm::mtrr: Unknown MTRR MSR {:#x}\n", index);
    }
}

void vm::VCPU::enter_smm() {
    vm::RegisterState regs{};
    get_regs(regs);

    uint8_t save[512] = {};
    auto put = [&](uint8_t sz, uint16_t offset, uint64_t value) {
        if(sz == 2)
            *(uint16_t*)(save + offset - 0x7E00) = value;
        else if(sz == 4)
            *(uint32_t*)(save + offset - 0x7E00) = value;
        else if(sz == 8)
            *(uint64_t*)(save + offset - 0x7E00) = value;
        else
            PANIC("Unknown size");
    };

    auto segment_flags = [](const RegisterState::Segment& seg) -> uint32_t {
        return (seg.attrib.g << 23) | (seg.attrib.db << 22) | (seg.attrib.l << 21) | \
               (seg.attrib.avl << 20) | (seg.attrib.present << 15) | (seg.attrib.dpl << 13) | \
               (seg.attrib.s << 12) | (seg.attrib.type << 8);
    };

    // Save CPU State
    {
        put(8, 0x7FF8, regs.rax);
        put(8, 0x7FF0, regs.rcx);
        put(8, 0x7FE8, regs.rdx);
        put(8, 0x7FE0, regs.rbx);
        put(8, 0x7FD8, regs.rsp);
        put(8, 0x7FD0, regs.rbp);
        put(8, 0x7FC8, regs.rsi);
        put(8, 0x7FC0, regs.rdi);
        put(8, 0x7FB8, regs.r8);
        put(8, 0x7FB0, regs.r9);
        put(8, 0x7FA8, regs.r10);
        put(8, 0x7FA0, regs.r11);
        put(8, 0x7F98, regs.r12);
        put(8, 0x7F90, regs.r13);
        put(8, 0x7F88, regs.r14);
        put(8, 0x7F80, regs.r15);

        put(8, 0x7F78, regs.rip);
        put(8, 0x7F70, regs.rflags);

        put(8, 0x7F68, regs.dr6);
        put(8, 0x7F60, regs.dr7);

        put(8, 0x7F58, regs.cr0);
        put(8, 0x7F50, regs.cr3);
        put(8, 0x7F48, regs.cr4);

        put(4, 0x7F00, smbase);

        put(4, 0x7EFC, 0x00020064); // Revision ID

        put(8, 0x7ED0, regs.efer);

        put(4, 0x7E84, regs.idtr.limit);
        put(8, 0x7E88, regs.idtr.base);

        put(4, 0x7E64, regs.gdtr.limit);
        put(8, 0x7E68, regs.gdtr.base);

        #define PUT_SEGMENT(i, name) \
            put(2, 0x7E00 + (i * 0x10), regs.name.selector); \
            put(2, 0x7E02 + (i * 0x10), segment_flags(regs.name) >> 8); \
            put(4, 0x7E04 + (i * 0x10), regs.name.limit); \
            put(8, 0x7E08 + (i * 0x10), regs.name.base)

        PUT_SEGMENT(0, es);
        PUT_SEGMENT(1, cs);
        PUT_SEGMENT(2, ss);
        PUT_SEGMENT(3, ds);
        PUT_SEGMENT(4, fs);
        PUT_SEGMENT(5, gs);
        PUT_SEGMENT(7, ldtr);
        PUT_SEGMENT(9, tr);
    }

    auto* dst = (uint8_t*)(vm->mm->get_phys(smbase + 0xFE00) + phys_mem_map);
    memcpy(dst, save, 512);

    regs.rflags = (1 << 1);
    regs.rip = 0x8000;
    regs.cr0 &= ~((1 << 0) | (1 << 2) | (1 << 3) | (1 << 31));
    regs.cr4 = cr4_constraint;
    regs.efer = efer_constraint;

    regs.idtr = {.base = 0, .limit = 0};
    regs.dr7 = 0x400;
    regs.cs = {.selector = (uint16_t)((smbase >> 4) & 0xFFFF), .base = smbase, .limit = 0xFFFF'FFFF, .attrib = {.type = 0b11, .s = 1, .present = 1, .g = 1}};

    regs.ds = {.selector = 0, .base = 0, .limit = 0xFFFF'FFFF, .attrib = {.type = 0b11, .s = 1, .present = 1, .g = 1}};
    regs.es = {.selector = 0, .base = 0, .limit = 0xFFFF'FFFF, .attrib = {.type = 0b11, .s = 1, .present = 1, .g = 1}};
    regs.ss = {.selector = 0, .base = 0, .limit = 0xFFFF'FFFF, .attrib = {.type = 0b11, .s = 1, .present = 1, .g = 1}};
    regs.fs = {.selector = 0, .base = 0, .limit = 0xFFFF'FFFF, .attrib = {.type = 0b11, .s = 1, .present = 1, .g = 1}};
    regs.gs = {.selector = 0, .base = 0, .limit = 0xFFFF'FFFF, .attrib = {.type = 0b11, .s = 1, .present = 1, .g = 1}};

    set_regs(regs);

    smm_entry_callback(smm_entry_userptr);

    is_in_smm = true;
}

void vm::VCPU::handle_rsm() {
    ASSERT(is_in_smm);

    uint8_t buf[512] = {};
    auto* src = (uint8_t*)(vm->mm->get_phys(smbase + 0xFE00) + phys_mem_map);
    memcpy(buf, src, 512);

    RegisterState rregs{};
    get_regs(rregs);

    // Allow CPU to return to real mode
    rregs.cr4 &= ~(1 << 17); // Clear cr4.PCIDE, before cr0.PG
    rregs.cs = {.selector = 0, .base = 0, .limit = 0, .attrib = {.type = 0xB, .s = 1, .present = 1, .g = 1}}; // 32bit CS is required to clear LMA

    if(rregs.cr0 & (1 << 0))
        rregs.cr0 &= ~((1 << 0) | (1 << 31)); // Clear cr0.PE and cr0.PG

    rregs.cr4 &= ~(1 << 5); // Clear cr4.PAE
    rregs.efer = efer_constraint;

    auto get = [&]<typename T>(uint8_t sz, uint16_t offset, T& value) {
        if(sz == 2)
            value = *(uint16_t*)(buf + offset - 0x7E00);
        else if(sz == 4)
            value = *(uint32_t*)(buf + offset - 0x7E00);
        else if(sz == 8)
            value = *(uint64_t*)(buf + offset - 0x7E00);
        else
            PANIC("Unknown size");
    };

    get(8, 0x7FF8, rregs.rax);
    get(8, 0x7FF0, rregs.rcx);
    get(8, 0x7FE8, rregs.rdx);
    get(8, 0x7FE0, rregs.rbx);
    get(8, 0x7FD8, rregs.rsp);
    get(8, 0x7FD0, rregs.rbp);
    get(8, 0x7FC8, rregs.rsi);
    get(8, 0x7FC0, rregs.rdi);
    get(8, 0x7FB8, rregs.r8);
    get(8, 0x7FB0, rregs.r9);        
    get(8, 0x7FA8, rregs.r10);
    get(8, 0x7FA0, rregs.r11);
    get(8, 0x7F98, rregs.r12);
    get(8, 0x7F90, rregs.r13);        
    get(8, 0x7F88, rregs.r14);
    get(8, 0x7F80, rregs.r15);

    get(8, 0x7F78, rregs.rip);
    get(8, 0x7F70, rregs.rflags); rregs.rflags |= (1 << 1);

    uint32_t dr6 = 0;
    get(4, 0x7F68, dr6);
    rregs.dr6 = (dr6 & 0x1'E00F) | 0xFFFE'0FF0;

    uint32_t dr7 = 0;
    get(4, 0x7F60, dr7);
    rregs.dr7 = (dr7 & 0xFFFF'2BFF) | 0x400;

    uint64_t cr0 = 0, cr3 = 0, cr4 = 0;
    get(8, 0x7F58, cr0);
    get(8, 0x7F50, cr3);
    get(8, 0x7F48, cr4);
    get(4, 0x7F00, smbase);

    get(8, 0x7ED0, rregs.efer); rregs.efer = (rregs.efer & (1 << 10)) | efer_constraint; // Make sure constrained bits are set, and LMA which is RO is clear

    #define GET_SEGMENT(i, name) \
        { \
            get(2, 0x7E00 + (i * 0x10), rregs.name.selector); \
            get(4, 0x7E04 + (i * 0x10), rregs.name.limit); \
            get(8, 0x7E08 + (i * 0x10), rregs.name.base); \
            uint32_t flags = 0; \
            get(2, 0x7E02 + (i * 0x10), flags); \
            flags <<= 8; \
            rregs.name.attrib.g = (flags >> 23) & 1; \
            rregs.name.attrib.db = (flags >> 22) & 1; \
            rregs.name.attrib.l = (flags >> 21) & 1; \
            rregs.name.attrib.avl = (flags >> 20) & 1; \
            rregs.name.attrib.present = (flags >> 15) & 1; \
            rregs.name.attrib.dpl = (flags >> 13) & 3; \
            rregs.name.attrib.s = (flags >> 12) & 1; \
            rregs.name.attrib.type = (flags >> 8) & 15; \
        }
        
    GET_SEGMENT(7, ldtr);
    GET_SEGMENT(9, tr);

    get(4, 0x7E84, rregs.idtr.limit);
    get(8, 0x7E88, rregs.idtr.base);

    get(4, 0x7E64, rregs.gdtr.limit);
    get(8, 0x7E68, rregs.gdtr.base);

    uint16_t pcid = 0;
    if(cr4 & (1 << 17)) {
        pcid = cr3 & 0xFFF;
        cr3 &= ~0xFFF;
    }

    rregs.cr3 = cr3;
    rregs.cr4 = (cr4 & ~(1 << 17)) | cr4_constraint;
    rregs.cr0 = cr0;
    if(cr4 & (1 << 17)) {
        rregs.cr4 = cr4;
        rregs.cr3 = cr3 | pcid;
    }


    GET_SEGMENT(0, es);
    GET_SEGMENT(1, cs);
    GET_SEGMENT(2, ss);
    GET_SEGMENT(3, ds);
    GET_SEGMENT(4, fs);
    GET_SEGMENT(5, gs);

    set_regs(rregs);

    smm_leave_callback(smm_leave_userptr);

    is_in_smm = false;
}

void vm::VCPU::dma_read(uintptr_t gpa, std::span<uint8_t> buf) {
    uintptr_t curr = 0;
    while(curr != buf.size_bytes()) {
        auto va = vm->mm->get_phys(gpa + curr) + phys_mem_map;
        auto top = align_up(va, pmm::block_size);


        auto chunk = min(top - va + 1, buf.size_bytes() - curr);

        memcpy(buf.data() + curr, (uint8_t*)va, chunk);

        curr += chunk;
    }
}

void vm::VCPU::dma_write(uintptr_t gpa, std::span<uint8_t> buf) {
    uintptr_t curr = 0;
    while(curr != buf.size_bytes()) {
        auto va = vm->mm->get_phys(gpa + curr) + phys_mem_map;
        auto top = align_up(va, pmm::block_size);

        auto chunk = min(top - va + 1, buf.size_bytes() - curr);

        memcpy((uint8_t*)va, buf.data() + curr, chunk);

        curr += chunk;
    }
}

vm::PageWalkInfo vm::VCPU::walk_guest_paging(uintptr_t gva) {
    vm::RegisterState regs{};
    get_regs(regs, VmRegs::Control); // We only really care about cr0, cr3, cr4, and efer here

    if(!(regs.cr0 & (1 << 31)))
        return {.found = true, .is_write = true, .is_user = true, .is_execute = true, .gpa = gva};

    // Paging is enabled, we can assume cr0.PE is true too, now figure out the various paging modes
    // But first look in the TLB
    auto off = gva & 0xFFF; 
    auto page = gva & 0xFFFF'F000;
    if(auto info = guest_tlb.lookup(page); info.found) {
        info.gpa += off;
        return info;
    }

    if(!(regs.efer & (1 << 10) && !(regs.cr4 & (1 << 5)))) { // No long mode and no PAE, thus normal 32bit paging
        bool write = true, user = true;
        auto pml2_i = (gva >> 22) & 0x3FF;
        auto pml1_i = (gva >> 12) & 0x3FF;

        uint32_t pml2_addr = regs.cr3 & 0xFFFF'F000;
        auto* pml2 = (uint32_t*)(vm->mm->get_phys(pml2_addr) + phys_mem_map); // Page tables are always in 1 page so this is fine
        uint32_t pml2_entry = pml2[pml2_i];

        ASSERT(pml2_entry & (1 << 0)); // Assert its present
        write = write && (pml2_entry >> 1) & 1;
        user = user && (pml2_entry >> 2) & 1;

        uint32_t pml1_addr = pml2_entry & 0xFFFF'F000;
        auto* pml1 = (uint32_t*)(vm->mm->get_phys(pml1_addr) + phys_mem_map); // Page tables are always in 1 page so this is fine
        uint32_t pml1_entry = pml1[pml1_i];
        
        ASSERT(pml1_entry & (1 << 0)); // Assert its present
        write = write && (pml1_entry >> 1) & 1;
        user = user && (pml1_entry >> 2) & 1;

        uint32_t gpa = pml1_entry & 0xFFFF'F000;
        guest_tlb.add(page, {.found = true, .is_write = write, .is_user = user, .is_execute = true, .gpa = gpa});
        return {.found = true, .is_write = write, .is_user = user, .is_execute = true, .gpa = gpa + off};
    } else {
        print("cr0: {:#x} cr4: {:#x} efer: {:#x}\n", regs.cr0, regs.cr4, regs.efer);
        PANIC("TODO");
    }
}

void vm::VCPU::mem_read(uintptr_t gva, std::span<uint8_t> buf) {
    uintptr_t curr = 0;
    while(curr != buf.size_bytes()) {
        auto gpa = walk_guest_paging(gva + curr).gpa;
        auto hpa = vm->mm->get_phys(gpa);
        auto hva = hpa + phys_mem_map;
        auto top = align_up(hva, pmm::block_size);

        auto chunk = min(top - hva + 1, buf.size_bytes() - curr);

        memcpy(buf.data() + curr, (uint8_t*)hva, chunk);

        curr += chunk;
    }
}

void vm::VCPU::mem_write(uintptr_t gva, std::span<uint8_t> buf) {
    uintptr_t curr = 0;
    while(curr != buf.size_bytes()) {
        auto res = walk_guest_paging(gva + curr);
        ASSERT(res.is_write);
        auto hpa = vm->mm->get_phys(res.gpa);
        auto hva = hpa + phys_mem_map;
        auto top = align_up(hva, pmm::block_size);

        auto chunk = min(top - hva + 1, buf.size_bytes() - curr);

        memcpy((uint8_t*)hva, buf.data() + curr, chunk);

        curr += chunk;
    }
}

vm::Vm::Vm(uint8_t n_cpus) {
    switch (get_cpu().cpu.vm.vendor) {
        case CpuVendor::Intel:
            mm = vmx::create_ept();
            break;
        case CpuVendor::AMD:
            mm = svm::create_npt();
            break;
        default:
            PANIC("Unknown virtualization vendor");
    }


    ASSERT(n_cpus > 0); // Make sure there's at least 1 VCPU
    for(uint8_t i = 0; i < n_cpus; i++)
        cpus.emplace_back(this, i);
}