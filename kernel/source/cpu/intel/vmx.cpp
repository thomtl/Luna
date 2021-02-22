#include <Luna/cpu/intel/vmx.hpp>

#include <Luna/cpu/cpu.hpp>
#include <Luna/cpu/regs.hpp>

#include <Luna/cpu/gdt.hpp>
#include <Luna/cpu/idt.hpp>
#include <Luna/cpu/tss.hpp>

#include <Luna/misc/log.hpp>

#include <Luna/mm/pmm.hpp>
#include <Luna/mm/vmm.hpp>

extern "C" {
    uint64_t vmx_vmlaunch(vmx::GprState* gprs); // These functions return rflags after vmlaunch or vmresume
    uint64_t vmx_vmresume(vmx::GprState* gprs);
}

constexpr const char* vm_instruction_errors[] = {
    "Reserved",
    "VMCALL executed in VMX root operation",
    "VMCLEAR with invalid physical address",
    "VMCLEAR with VMXON pointer",
    "VMLAUNCH with non-clear VMCS",
    "VMRESUME with non-launched VMCS",
    "VMRESUME after VMXOFF",
    "VM entry with invalid control field(s)",
    "VM entry with invalid host-state field(s)",
    "VMPTRLD with invalid physical address",
    "VMPTRLD with VMXON pointer",
    "VMPTRLD with incorrect VMCS revision identifier",
    "VM{READ,WRITE} to unsupported VMCS field",
    "VMWRITE to read-only VMCS field",
    "Reserved",
    "VMXON executed in VMX root operation",
    "VM entry with invalid executive-VMCS pointer",
    "VM entry with non-launched VMCS",
    "VM entry with executive-VMCS pointer not VMXON pointer",
    "VMCALL with non-clear VMCS",
    "VMCALL with invalid VM-exit control field(s)",
    "Reserved",
    "VMCALL with incorrect MSEG revision identifier",
    "VMXOFF under dual-monitor treatment of SMIs and SMM",
    "VMCALL with invalid SMM-monitor features",
    "VM entry with invalid VM-execution control fields in executive VMCS",
    "VM entry with events blocked by MOV SS",
    "Reserved",
    "Invalid operand to INVEPT/INVVPID"
};

bool vmx::is_supported() {
 uint32_t a, b, c, d;
    ASSERT(cpu::cpuid(1, a, b, c, d));

    if((c & (1 << 5)) == 0)
        return false; // Unsupported

    return true;
}

static uint64_t cr_constraint(uint32_t msr0, uint32_t msr1) {
    uint64_t cr = 0;
    auto fixed0 = msr::read(msr0);
    auto fixed1 = msr::read(msr1);

    cr &= fixed1;
    cr |= fixed0;

    return cr;
}

uint64_t vmx::get_cr0_constraint() {
    return cr_constraint(msr::ia32_vmx_cr0_fixed0, msr::ia32_vmx_cr0_fixed1);
}

uint64_t vmx::get_cr4_constraint() {
    return cr_constraint(msr::ia32_vmx_cr4_fixed0, msr::ia32_vmx_cr4_fixed1);
}

void vmx::init() {
     uint32_t a, b, c, d;
    ASSERT(cpu::cpuid(1, a, b, c, d));

    if((c & (1 << 5)) == 0)
        return; // Unsupported

    if(auto feature = msr::read(msr::ia32_feature_control); (feature & 0x5) != 0x5)
        msr::write(msr::ia32_feature_control, feature | 5); // Some SMX stuff idk


    uint64_t cr0 = cr0::read();
    cr0 &= msr::read(msr::ia32_vmx_cr0_fixed1);
	cr0 |= msr::read(msr::ia32_vmx_cr0_fixed0);
	cr0::write(cr0);

	uint64_t cr4 = cr4::read();
	cr4 &= msr::read(msr::ia32_vmx_cr4_fixed1);
	cr4 |= msr::read(msr::ia32_vmx_cr4_fixed0);
	cr4::write(cr4);

    cr4::write(cr4::read() | (1 << 13)); // Set cr4.VMXE

    auto basic = msr::read(msr::ia32_vmx_basic);
    auto revision = basic & 0x7FFF'FFFF;
    auto vmcs_size = (basic >> 32) & 0x1FFFF;
    ASSERT(vmcs_size <= pmm::block_size);

    ASSERT(((basic >> 48) & 1) == 0);

    auto vmxon_region_pa = pmm::alloc_block();
    ASSERT(vmxon_region_pa);
    volatile auto* vmxon_region = (uint32_t*)(vmxon_region_pa + phys_mem_map);

    *vmxon_region = revision; // What in the world intel???????, WHY DO YOU NEED THIS
    
    bool success = false;
    asm volatile("vmxon %[Region]" : "=@ccnc"(success) : [Region]"m"(vmxon_region_pa) : "memory");
    if(!success)
        PANIC("'vmxon' Failed");

    auto proc = msr::read(msr::ia32_vmx_procbased_ctls) >> 32;
    auto proc2 = msr::read(msr::ia32_vmx_procbased_ctls2) >> 32;

    if((proc & (uint32_t)ProcBasedControls::SecondaryControlsEnable) == 0)
        PANIC("Secondary ProcBasedCtls are unsupported");
    
    if((proc2 & (uint32_t)ProcBasedControls2::EPTEnable) == 0)
        PANIC("EPT is unsupported");
    
    if((proc2 & (uint32_t)ProcBasedControls2::UnrestrictedGuest) == 0)
        PANIC("Unrestricted Guest is unsupported");

    auto& cpu = get_cpu().cpu;
    auto ept = msr::read(msr::ia32_vmx_ept_vpid_cap);
    ASSERT((ept >> 14) & 1); // Assert that WB paging is supported
    if((ept >> 6) & 1)
        cpu.vmx.ept_levels = 4;
    else
        PANIC("Unknown amount of EPT levels");

    cpu.vmx.ept_dirty_accessed = (ept >> 21) & 1;

    ASSERT(ept & (1 << 20)); // Assert invept is supported
    ASSERT(ept & (1 << 25)); // Assert single context invept is supported
}

ept::context* vmx::create_ept() {
    return new ept::context{get_cpu().cpu.vmx.ept_levels};
}

vmx::Vm::Vm(vm::AbstractMM* mm, vm::VCPU* vcpu): mm{mm}, vcpu{vcpu} {
    vmcs_pa = pmm::alloc_block();
    vmcs = vmcs_pa + phys_mem_map;
    memset((void*)vmcs, 0, pmm::block_size);

    volatile auto* vmcs_revision = (uint32_t*)vmcs;
    *vmcs_revision = msr::read(msr::ia32_vmx_basic) & 0x7FFF'FFFF;

    vmptrld();

    write(vmcs_link_pointer, -1ll);

    auto adjust_controls = [&](uint32_t min, uint32_t opt, uint32_t msr) -> uint32_t {
        uint32_t ctl = min | opt;

        auto constraint = msr::read(msr);
        auto lo = (constraint & 0xFFFF'FFFF);
        auto hi = (constraint >> 32);

        ctl &= hi;
        ctl |= lo;

        ASSERT((min & ~ctl) == 0);

        return ctl;
    };

    {
        uint32_t min = (uint32_t)PinBasedControls::NMI;// | (uint32_t)PinBasedControls::ExtInt
        uint32_t opt = 0;
        write(pin_based_vm_exec_controls, adjust_controls(min, opt, msr::ia32_vmx_pinbased_ctls));
    }

    {
        uint32_t min = (uint32_t)ProcBasedControls::VMExitOnHlt | (uint32_t)ProcBasedControls::VMExitOnPIO \
                     | (uint32_t)ProcBasedControls::SecondaryControlsEnable \
                     | (uint32_t)ProcBasedControls::VMExitOnCr8Store \
                     | (uint32_t)ProcBasedControls::VMExitOnRdpmc \
                     | (uint32_t)ProcBasedControls::TSCOffsetting;
        uint32_t opt = 0;
        write(proc_based_vm_exec_controls, adjust_controls(min, opt, msr::ia32_vmx_procbased_ctls));
    }

    {
        uint32_t min = (uint32_t)ProcBasedControls2::EPTEnable \
                     | (uint32_t)ProcBasedControls2::UnrestrictedGuest;
        uint32_t opt = 0;
        write(proc_based_vm_exec_controls2, adjust_controls(min, opt, msr::ia32_vmx_procbased_ctls2));
    }
    
    write(exception_bitmap, (1 << 1) | (1 << 6) | (1 << 14) | (1 << 17) | (1 << 18));

    {
        uint32_t min = (uint32_t)VMExitControls::LongMode | (uint32_t)VMExitControls::LoadIA32EFER;
        uint32_t opt = 0;
        write(vm_exit_control, adjust_controls(min, opt, msr::ia32_vmx_exit_ctls));
    }

    {
        uint32_t min = (uint32_t)VMEntryControls::LoadIA32EFER;
        uint32_t opt = 0;
        write(vm_entry_control, adjust_controls(min, opt, msr::ia32_vmx_entry_ctls));
    }

    {
        uint64_t eptp = 0;
        eptp |= mm->get_root_pa(); // Set EPT Physical Address
        eptp |= ((mm->get_levels() - 1) << 3); // Set EPT Number of page levels
        eptp |= 6; // Writeback Caching

        if(get_cpu().cpu.vmx.ept_dirty_accessed)
            eptp |= (1 << 6);

        write(ept_control, eptp);
    }

    write(guest_interruptibility_state, 0);
    write(guest_activity_state, 0);

    //write(guest_intr_status, 0); // Only do if we use Virtual-Interrupt Delivery
    //write(guest_pml_index, 0); // Only do if we do PMLs

    write(host_tr_base, (uint64_t)&get_cpu().tss_table); // We don't have a TSS

    {
        gdt::pointer gdtr{};
        gdtr.store();

        write(host_gdtr_base, gdtr.table);
    }

    {
        idt::pointer idtr{};
        idtr.store();

        write(host_idtr_base, idtr.table);
    }

    write(host_cr0, cr0::read());
    write(host_cr4, cr4::read());
    write(host_pat_full, msr::read(msr::ia32_pat));
    write(host_efer_full, msr::read(msr::ia32_efer));
}

void vmx::Vm::set(vm::VmCap cap, bool value) {
    vmptrld();
    if(cap == vm::VmCap::FullPIOAccess) {
        if(value)
            write(proc_based_vm_exec_controls, read(proc_based_vm_exec_controls) & ~(uint32_t)ProcBasedControls::VMExitOnPIO);
        else
            write(proc_based_vm_exec_controls, read(proc_based_vm_exec_controls) | (uint32_t)ProcBasedControls::VMExitOnPIO);
    }
}

bool vmx::Vm::run(vm::VmExit& exit) {
    vmptrld();

    #define SAVE_REG(reg) \
        { \
            uint16_t tmp = 0; \
            asm volatile("mov %%"#reg", %0" : "=r"(tmp) : : "memory"); \
            write(host_##reg##_sel, tmp); \
        }

    SAVE_REG(cs);
    SAVE_REG(ds);
    SAVE_REG(ss);
    SAVE_REG(es);
    SAVE_REG(fs);
    SAVE_REG(gs);

    write(host_tr_sel, tss::Table::store());

    uint64_t cr3 = 0;
    asm volatile("mov %%cr3, %0" : "=r"(cr3) : : "memory");
    write(host_cr3, cr3);

    write(host_fs_base, msr::read(msr::fs_base));
    write(host_gs_base, msr::read(msr::gs_base));

    bool launched = false;
    while(true) {
        asm("cli");

        vmclear();
        vmptrld();

        write(tsc_offset, -cpu::rdtsc() + vcpu->tsc);

        host_simd.store();
        guest_simd.load();

        uint64_t rflags = 0;
        if(!launched) {
            rflags = vmx_vmlaunch(&guest_gprs);
            launched = true;
        } else {
            rflags = vmx_vmresume(&guest_gprs);
        }

        guest_simd.store();
        host_simd.load();

        vcpu->tsc = cpu::rdtsc() + tsc_offset;

        // VM Exits restore the GDT and IDT Limit to 0xFFFF for some reason, so fix them
        get_cpu().gdt_table.set();
        idt::load();

        asm("sti");

        // rflags.CF is set when an error occurs and there is no current VMCS
        if(rflags & (1 << 0)) {
            print("vmx: VMExit error without valid VMCS\n");
            return false;
        } else if(rflags & (1 << 6)) { // rflags.ZF is set when an error occurs and there is a VMCS, error code is registered in vm_instruction_error
            auto error = read(vm_instruction_error);
            print("vmx: VMExit error: {:s} ({:#x})\n", vm_instruction_errors[error], error);
            return false;
        }

        auto next_instruction = [&]() { write(guest_rip, read(guest_rip) + exit.instruction_len); };

        auto basic_reason = (VMExitReasons)(read(vm_exit_reason) & 0xFFFF);
        if(basic_reason == VMExitReasons::Exception) {
            InterruptionInfo info{.raw = (uint32_t)read(vm_exit_interruption_info)};
            auto grip = read(guest_cs_base) + read(guest_rip);
            // Hardware exception
            if(info.type == 3) {
                if(info.vector == 6) { // #UD
                    auto* instruction = (uint8_t*)(mm->get_phys(grip) + phys_mem_map); // TODO: Make sure this doesn't cross page boundaries

                    // Make sure we can run AMD's VMMCALL on Intel
                    if(instruction[0] == 0x0F && instruction[1] == 0x01 && instruction[2] == 0xD9) {
                        exit.reason = vm::VmExit::Reason::Vmcall;

                        exit.instruction_len = 3;
                        exit.instruction[0] = 0x0F;
                        exit.instruction[1] = 0x01;
                        exit.instruction[2] = 0xD9;

                        next_instruction();

                        return true;
                    } else if(instruction[0] == 0x0F && instruction[1] == 0xAA) {
                        exit.reason = vm::VmExit::Reason::RSM;

                        exit.instruction_len = 2;
                        exit.instruction[0] = 0x0F;
                        exit.instruction[1] = 0xAA;

                        next_instruction();

                        return true;
                    } else {
                        print("VT-x: #UD: gRIP: {:#x} : ", grip);

                        for(uint8_t i = 0; i < 15; i++)
                            print("{:x} ", instruction[i]);

                        print("\n");
                    }
                }

                auto type = info.type, vector = info.vector;
                print("VT-x: Interruption: Type: {}, Vector: {}\n", type, vector);
                return false;
            }
        } else if(basic_reason == VMExitReasons::CPUID) {
            exit.reason = vm::VmExit::Reason::CPUID;

            exit.instruction_len = 2;
            exit.instruction[0] = 0x0F;
            exit.instruction[1] = 0xA2;

            next_instruction();

            return true;
        } else if(basic_reason == VMExitReasons::Hlt) {
            exit.reason = vm::VmExit::Reason::Hlt;

            exit.instruction_len = 1;
            exit.instruction[0] = 0xF4;

            next_instruction();

            return true;
        } else if(basic_reason == VMExitReasons::Vmcall) {
            exit.reason = vm::VmExit::Reason::Vmcall;

            exit.instruction_len = 3;
            exit.instruction[0] = 0x0F;
            exit.instruction[1] = 0x01;
            exit.instruction[2] = 0xC1;

            next_instruction();

            return true;
        } else if(basic_reason == VMExitReasons::PIO) {
            IOQualification info{.raw = read(vm_exit_qualification)};

            exit.reason = vm::VmExit::Reason::PIO;

            exit.instruction_len = read(vm_exit_instruction_len);

            exit.pio.size = info.size + 1;
            exit.pio.port = info.port;
            exit.pio.rep = info.rep;
            exit.pio.string = info.string;
            exit.pio.write = !info.dir;

            next_instruction();

            return true;

        } else if(basic_reason == VMExitReasons::Rdmsr) {
            exit.reason = vm::VmExit::Reason::MSR;

            exit.msr.write = false;
            exit.instruction_len = 2;

            exit.instruction[0] = 0x0F;
            exit.instruction[1] = 0x32;

            next_instruction();

            return true;
        } else if(basic_reason == VMExitReasons::Wrmsr) {
            exit.reason = vm::VmExit::Reason::MSR;

            exit.msr.write = true;
            exit.instruction_len = 2;

            exit.instruction[0] = 0x0F;
            exit.instruction[1] = 0x30;

            next_instruction();

            return true;
        } else if(basic_reason == VMExitReasons::InvalidGuestState) {
            print("vmx: VM-Entry Failure due to invalid guest state\n");
            return false;
        } else if(basic_reason == VMExitReasons::EPTViolation) {
            auto addr = read(ept_violation_addr);
            EPTViolationQualification info{.raw = read(vm_exit_qualification)};

            exit.reason = vm::VmExit::Reason::MMUViolation;

            exit.mmu.access.r = info.r;
            exit.mmu.access.w = info.w;
            exit.mmu.access.x = info.x;
            exit.mmu.access.user = 0;

            exit.mmu.page.present = info.page_r;
            exit.mmu.page.r = info.page_r;
            exit.mmu.page.w = info.page_w;
            exit.mmu.page.x = info.page_x;
            exit.mmu.page.user = 0;

            exit.mmu.gpa = addr;
            exit.mmu.reserved_bits_set = false;

            return true;
        } else {
            print("vmx: Unknown VMExit Reason: {:d}\n", (uint64_t)basic_reason);
            PANIC("Unknown exit reason");
        }
    }
}

void vmx::Vm::inject_int(vm::AbstractVm::InjectType type, uint8_t vector, bool error_code, uint32_t error) {
    uint8_t type_val = 0;
    switch (type) {
        case vm::AbstractVm::InjectType::Exception: type_val = 3; break; 
        case vm::AbstractVm::InjectType::NMI: type_val = 2; break;
        case vm::AbstractVm::InjectType::ExtInt: type_val = 0; break;
        default:
            PANIC("Unsupported Injection type"); // TODO: Support software ints
    }

    uint32_t info = 0;
    info |= vector;
    info |= (type_val << 8);
    info |= (error_code << 11);
    info |= (1 << 31); // Valid

    if(error_code)
        write(vm_entry_exception_error_code, error);

    write(vm_entry_interruption_info, info);
}

void vmx::Vm::get_regs(vm::RegisterState& regs) const {
    vmptrld();

    regs.rax = guest_gprs.rax;
    regs.rbx = guest_gprs.rbx;
    regs.rcx = guest_gprs.rcx;
    regs.rdx = guest_gprs.rdx;
    regs.rsi = guest_gprs.rsi;
    regs.rdi = guest_gprs.rdi;
    regs.rbp = guest_gprs.rbp;

    regs.r8 = guest_gprs.r8;
    regs.r9 = guest_gprs.r9;
    regs.r10 = guest_gprs.r10;
    regs.r11 = guest_gprs.r11;
    regs.r12 = guest_gprs.r12;
    regs.r13 = guest_gprs.r13;
    regs.r14 = guest_gprs.r14;
    regs.r15 = guest_gprs.r15;

    regs.dr0 = guest_gprs.dr0;
    regs.dr1 = guest_gprs.dr1;
    regs.dr2 = guest_gprs.dr2;
    regs.dr3 = guest_gprs.dr3;
    regs.dr6 = guest_gprs.dr6;

    regs.rsp = read(guest_rsp);
    regs.rip = read(guest_rip);
    regs.rflags = read(guest_rflags);
    regs.dr7 = read(guest_dr7);

    regs.cr0 = read(guest_cr0);
    regs.cr3 = read(guest_cr3);
    regs.cr4 = read(guest_cr4);
    regs.efer = read(guest_efer_full);

    #define GET_TABLE(table) \
        regs.table.base = read(guest_##table##_base); \
        regs.table.limit = read(guest_##table##_limit)

    GET_TABLE(gdtr);
    GET_TABLE(idtr);

    #define GET_SEGMENT(segment) \
        regs.segment.base = read(guest_##segment##_base); \
        regs.segment.limit = read(guest_##segment##_limit); \
        regs.segment.selector = read(guest_##segment##_selector); \
        { \
            auto seg = read(guest_##segment##_access_right); \
            regs.segment.attrib.type = seg & 0xF; \
            regs.segment.attrib.s = (seg >> 4) & 1; \
            regs.segment.attrib.dpl = (seg >> 5) & 3; \
            regs.segment.attrib.present = (seg >> 7) & 1; \
            regs.segment.attrib.avl = (seg >> 12) & 1; \
            regs.segment.attrib.l = (seg >> 13) & 1; \
            regs.segment.attrib.db = (seg >> 14) & 1; \
            regs.segment.attrib.g = (seg >> 15) & 1; \
        } 

    GET_SEGMENT(cs);
    GET_SEGMENT(ds);
    GET_SEGMENT(ss);
    GET_SEGMENT(es);
    GET_SEGMENT(fs);
    GET_SEGMENT(gs);

    GET_SEGMENT(ldtr);
    GET_SEGMENT(tr);
}

void vmx::Vm::set_regs(const vm::RegisterState& regs) {
    vmptrld();

    guest_gprs.rax = regs.rax;
    guest_gprs.rbx = regs.rbx;
    guest_gprs.rcx = regs.rcx;
    guest_gprs.rdx = regs.rdx;
    guest_gprs.rsi = regs.rsi;
    guest_gprs.rdi = regs.rdi;
    guest_gprs.rbp = regs.rbp;

    guest_gprs.r8 = regs.r8;
    guest_gprs.r9 = regs.r9;
    guest_gprs.r10 = regs.r10;
    guest_gprs.r11 = regs.r11;
    guest_gprs.r12 = regs.r12;
    guest_gprs.r13 = regs.r13;
    guest_gprs.r14 = regs.r14;
    guest_gprs.r15 = regs.r15;

    guest_gprs.dr0 = regs.dr0;
    guest_gprs.dr1 = regs.dr1;
    guest_gprs.dr2 = regs.dr2;
    guest_gprs.dr3 = regs.dr3;
    guest_gprs.dr6 = regs.dr6;

    write(guest_rsp, regs.rsp);
    write(guest_rip, regs.rip);
    write(guest_rflags, regs.rflags);
    write(guest_dr7, regs.dr7);

    write(guest_cr0, regs.cr0);
    write(guest_cr4, regs.cr4);
    write(guest_cr3, regs.cr3);
    write(guest_efer_full, regs.efer);

    #define SET_TABLE(table) \
        write(guest_##table##_base, regs.table.base); \
        write(guest_##table##_limit, regs.table.limit)

    SET_TABLE(gdtr);
    SET_TABLE(idtr);

    #define SET_SEGMENT(segment) \
        write(guest_##segment##_base, regs.segment.base); \
        write(guest_##segment##_limit, regs.segment.limit); \
        write(guest_##segment##_selector, regs.segment.selector); \
        { \
            uint32_t attrib = regs.segment.attrib.type | (regs.segment.attrib.s << 4) | \
                              (regs.segment.attrib.dpl << 5) | (regs.segment.attrib.present << 7) | \
                              (regs.segment.attrib.avl << 12) | (regs.segment.attrib.l << 13) | \
                              (regs.segment.attrib.db << 14) | (regs.segment.attrib.g << 15); \
            write(guest_##segment##_access_right, attrib); \
        }

    SET_SEGMENT(cs);
    SET_SEGMENT(ds);
    SET_SEGMENT(ss);
    SET_SEGMENT(es);
    SET_SEGMENT(fs);
    SET_SEGMENT(gs);

    SET_SEGMENT(ldtr);
    SET_SEGMENT(tr);
}

void vmx::Vm::vmptrld() const {
    bool success = false;
    asm volatile("vmptrld %[Vmcs]" : "=@cca"(success) : [Vmcs] "m"(vmcs_pa) : "memory");
    ASSERT(success);
}

void vmx::Vm::vmclear() {
    bool success = false;
    asm volatile("vmclear %[Vmcs]" : "=@cca"(success) : [Vmcs] "m"(vmcs_pa) : "memory");
    ASSERT(success);
}

void vmx::Vm::write(uint64_t field, uint64_t value) {
    bool success = false;
    asm volatile("vmwrite %[Value], %[Field]" : "=@cca"(success) : [Field] "r"(field), [Value] "rm"(value) : "memory");
    ASSERT(success);
}

uint64_t vmx::Vm::read(uint64_t field) const {
    uint64_t ret = 0;
    bool success = false;
    asm volatile("vmread %[Field], %[Value]" : "=@cca"(success), [Value] "=rm"(ret) : [Field] "r"(field) : "memory");
    ASSERT(success);

    return ret;
}