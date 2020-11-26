#include <Luna/cpu/intel/vmx.hpp>

#include <Luna/cpu/cpu.hpp>
#include <Luna/cpu/regs.hpp>

#include <Luna/cpu/gdt.hpp>
#include <Luna/cpu/idt.hpp>
#include <Luna/cpu/tss.hpp>

#include <Luna/misc/format.hpp>

#include <Luna/mm/pmm.hpp>
#include <Luna/mm/vmm.hpp>

extern "C" {
    void vmx_vmlaunch(vmx::GprState* gprs);
    void vmx_vmresume(vmx::GprState* gprs);
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

    auto& cpu = get_cpu().cpu;
    auto ept = msr::read(msr::ia32_vmx_ept_vpid_cap);
    ASSERT((ept >> 14) & 1); // Assert that WB paging is supported
    if((ept >> 6) & 1)
        cpu.vmx.ept_levels = 4;
    else
        PANIC("Unknown amount of EPT levels");

    cpu.vmx.ept_dirty_accessed = ((ept >> 21) & 1) ? true : false;
}

vmx::Vm::Vm(): guest_page{get_cpu().cpu.vmx.ept_levels} {
    vmcs_pa = pmm::alloc_block();
    vmcs = vmcs_pa + phys_mem_map;

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
        uint32_t min = (uint32_t)PinBasedControls::ExtInt;
        uint32_t opt = 0;
        write(pin_based_vm_exec_controls, adjust_controls(min, opt, msr::ia32_vmx_pinbased_ctls));
    }

    {
        uint32_t min = (uint32_t)ProcBasedControls::VMExitOnHlt | (uint32_t)ProcBasedControls::VMExitOnPIO | (uint32_t)ProcBasedControls::SecondaryControlsEnable;
        uint32_t opt = 0;
        write(proc_based_vm_exec_controls, adjust_controls(min, opt, msr::ia32_vmx_procbased_ctls));
    }

    {
        uint32_t min = (uint32_t)ProcBasedControls2::EPTEnable | (uint32_t)ProcBasedControls2::UnrestrictedGuest | (uint32_t)ProcBasedControls2::VMExitOnDescriptor;
        uint32_t opt = 0;
        write(proc_based_vm_exec_controls2, adjust_controls(min, opt, msr::ia32_vmx_procbased_ctls2));

    }
    
    write(exception_bitmap, 0);

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

    write(host_cr0, cr0::read() | msr::read(msr::ia32_vmx_cr0_fixed0));
    write(host_cr4, cr4::read() | msr::read(msr::ia32_vmx_cr4_fixed0));
    //write(host_pat_full, msr::read(msr::ia32_pat));
    write(host_efer_full, msr::read(msr::ia32_efer));

    constexpr uint16_t code_access = 0b11 | (1 << 4) | (1 << 7) | (1 << 13);
    constexpr uint16_t data_access = 0b11 | (1 << 4) | (1 << 7);
    constexpr uint16_t ldtr_access = 0b10 | (1 << 7);
    constexpr uint16_t tr_access = 0b11 | (1 << 7);

    write(guest_es_selector, 0);
    write(guest_es_base, 0);
    write(guest_es_limit, 0xFFFF);
    write(guest_es_access_right, data_access);

    write(guest_cs_selector, 0);
    write(guest_cs_base, 0);
    write(guest_cs_limit, 0xFFFF);
    write(guest_cs_access_right, code_access);

    write(guest_ds_selector, 0);
    write(guest_ds_base, 0);
    write(guest_ds_limit, 0xFFFF);
    write(guest_ds_access_right, data_access);

    write(guest_fs_selector, 0);
    write(guest_fs_base, 0);
    write(guest_fs_limit, 0xFFFF);
    write(guest_fs_access_right, data_access);

    write(guest_gs_selector, 0);
    write(guest_gs_base, 0);
    write(guest_gs_limit, 0xFFFF);
    write(guest_gs_access_right, data_access);

    write(guest_ss_selector, 0);
    write(guest_ss_base, 0);
    write(guest_ss_limit, 0xFFFF);
    write(guest_ss_access_right, data_access);

    write(guest_tr_selector, 0);
    write(guest_tr_base, 0);
    write(guest_tr_limit, 0xFFFF);
    write(guest_tr_access_right, tr_access);
    
    write(guest_ldtr_selector, 0);
    write(guest_ldtr_base, 0);
    write(guest_ldtr_limit, 0xFFFF);
    write(guest_ldtr_access_right, ldtr_access);

    write(guest_idtr_base, 0);
    write(guest_idtr_limit, 0xFFFF);

    write(guest_gdtr_base, 0);
    write(guest_gdtr_limit, 0xFFFF);

    write(guest_interruptibility_state, 0);
    write(guest_activity_state, 0);
    write(guest_intr_status, 0);
    write(guest_pml_index, 0);

    write(guest_dr7, 0);
    write(guest_rsp, 0);
    write(guest_rip, 0x1000);
    write(guest_rflags, 1 << 1); // Reserved bit
    write(guest_efer_full, 0);

    write(guest_cr3, 0);

    {
        auto cr4 = msr::read(msr::ia32_vmx_cr4_fixed0);
        auto lo = cr4 & 0xFFFF'FFFF;
        auto hi = cr4 >> 32;

        write(guest_cr4, lo | ((uint64_t)hi) << 32);

    }

    {
        auto cr0 = msr::read(msr::ia32_vmx_cr0_fixed0);
        auto lo = cr0 & 0xFFFF'FFFF;
        auto hi = cr0 >> 32;

        lo &= ~(1 << 0); // Clear cr0.PE
        lo &= ~(1 << 31); // Clear cr0.PG

        write(guest_cr0, lo | ((uint64_t)hi) << 32);
    }

    {
        uint64_t eptp = 0;
        eptp |= guest_page.get_root_pa(); // Set EPT Physical Address
        eptp |= ((guest_page.get_levels() - 1) << 3); // Set EPT Number of page levels
        eptp |= 6; // Writeback Caching

        if(get_cpu().cpu.vmx.ept_dirty_accessed)
            eptp |= (1 << 6);

        write(ept_control, eptp);
    }
}

void vmx::Vm::run() {
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

        host_simd.store();
        guest_simd.load();

        if(!launched) {
            vmx_vmlaunch(&guest_gprs);
            launched = true;
        } else {
            vmx_vmresume(&guest_gprs);
        }

        guest_simd.store();
        host_simd.load();

        // VM Exits restore the GDT and IDT Limit to 0xFFFF for some reason, so fix them
        get_cpu().gdt_table.set();
        idt::load();

        asm("sti");

        auto error = read(vm_instruction_error);
        if(error) {
            print("vmx: VMExit error: {:s} ({:#x})\n", vm_instruction_errors[error], error);
            return;
        }

        auto reason = (VMExitReasons)read(vm_exit_reason);
        if(reason == VMExitReasons::EPTViolation) {
            print("vmx: EPT Violation\n");
            auto addr = read(ept_violation_addr);
            EPTViolationQualification info{.raw = read(ept_violation_flags)};

            print("    GPA: {:#x}\n", addr);
            print("    Access: {:s}{:s}{:s}\n", info.r ? "R" : "", info.w ? "W" : "", info.x ? "X" : "");
            print("    Page: {:s}{:s}{:s}\n", info.page_r ? "R" : "", info.page_w ? "W" : "", info.page_x ? "X" : "");
            print("    {:s}\n", info.gva_translated ? "GVA Translated" : "");
            PANIC("EPT Violation");
        } else {
            print("vmx: Unknown VMExit Reason: {:#x}\n", (uint64_t)reason);
            PANIC("Unknown exit reason");
        }
    }
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

    regs.rsp = read(guest_rsp);
    regs.rip = read(guest_rip);
    regs.rflags = read(guest_rflags);

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
        regs.segment.attrib = read(guest_##segment##_access_right); \
        regs.segment.selector = read(guest_##segment##_selector)

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

    write(guest_rsp, regs.rsp);
    write(guest_rip, regs.rip);
    write(guest_rflags, regs.rflags);

    write(guest_cr0, regs.cr0);
    write(guest_cr3, regs.cr3);
    write(guest_cr4, regs.cr4);
    write(guest_efer_full, regs.efer);

    #define SET_TABLE(table) \
        write(guest_##table##_base, regs.table.base); \
        write(guest_##table##_limit, regs.table.limit)

    SET_TABLE(gdtr);
    SET_TABLE(idtr);

    #define SET_SEGMENT(segment) \
        write(guest_##segment##_base, regs.segment.base); \
        write(guest_##segment##_limit, regs.segment.limit); \
        write(guest_##segment##_access_right, regs.segment.attrib); \
        write(guest_##segment##_selector, regs.segment.selector)

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