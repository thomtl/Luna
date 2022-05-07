#include <Luna/cpu/amd/svm.hpp>
#include <Luna/cpu/cpu.hpp>

#include <std/string.hpp>

#include <Luna/misc/log.hpp>

void svm::init() {
    ASSERT(svm::is_supported());

    uint32_t a, b, c, d;
    if(!cpu::cpuid(0x8000'000A, a, b, c, d))
        PANIC("SVM support was detected but its CPUID leaf does not exist");

    if(msr::read(msr::vm_cr) & (1 << 4)) {
        if(d & (1 << 2))
            PANIC("SVM disabled with key");
        else
            PANIC("SVM disabled in BIOS");
    }

    auto& svm = get_cpu().cpu.svm;
    svm.n_asids = b;

    if(!(d & (1 << 0)))
        PANIC("Required feature NPT is unsupported");

    msr::write(msr::ia32_efer, msr::read(msr::ia32_efer) | (1 << 12));

    auto hsave = pmm::alloc_block();
    ASSERT(hsave);

    msr::write(msr::vm_hsave_pa, hsave);

    svm.asid_manager.init(svm.n_asids);
}

npt::context* svm::create_npt() {
    return new npt::context{4};
}

bool svm::is_supported() {
    uint32_t a, b, c, d;
    if(!cpu::cpuid(0x8000'0001, a, b, c, d))
        return false;

    if(!(c & (1 << 2)))
        return false;

    return true;
}

uint64_t svm::get_cr0_constraint() {
    return (1 << 29) | (1 << 30) | (1 << 4);
}

uint64_t svm::get_efer_constraint() {
    return (1 << 12);
}

svm::Vm::Vm(vm::AbstractMM* mm, vm::VCPU* vcpu): mm{mm}, vcpu{vcpu} {
    vmcb_pa = pmm::alloc_block();
    ASSERT(vmcb_pa);

    vmcb = (Vmcb*)(vmcb_pa + phys_mem_map);
    memset((void*)vmcb, 0, pmm::block_size);

    vmcb->icept_exceptions = (1 << 1) | (1 << 6) | (1 << 14) | (1 << 17) | (1 << 18);
    vmcb->icept_cr_writes = (1 << 8);

    vmcb->icept_vmrun = 1;
    vmcb->icept_vmmcall = 1;
    vmcb->icept_vmload = 1;
    vmcb->icept_vmsave = 1;

    vmcb->icept_intr = 1;
    vmcb->icept_nmi = 1;
    vmcb->icept_smi = 1;

    vmcb->icept_task_switch = 1;

    vmcb->icept_hlt = 1;
    vmcb->icept_cpuid = 1;
    vmcb->icept_invlpg = 1;
    vmcb->icept_rdpmc = 1;
    vmcb->icept_invd = 1;
    vmcb->icept_stgi = 1;
    vmcb->icept_clgi = 1;
    vmcb->icept_skinit = 1;
    vmcb->icept_xsetbv = 1;
    //vmcb->icept_wbinvd = 1;
    vmcb->icept_rdpru = 1;
    vmcb->icept_rsm = 1;

    vmcb->icept_rdtsc = 0; // TSC is handled by using tsc_offset, so we don't have to intercept it

    vmcb->icept_efer_write = 1;
    vmcb->v_intr_masking = 1;


    vmcb->npt_enable = 1;
    vmcb->npt_cr3 = mm->get_root_pa();
    vmcb->pat = 0x0007040600070406; // Default PAT

    vmcb->guest_asid = mm->get_asid();
    vmcb->tlb_control = 0; // Do no TLB flushes on vmrun, all TLB flushes are done by the NPT using invlpga

    io_bitmap_pa = pmm::alloc_n_blocks(io_bitmap_size);
    io_bitmap = (uint8_t*)(io_bitmap_pa + phys_mem_map);

    memset(io_bitmap, 0xFF, io_bitmap_size * pmm::block_size); // Intercept everything
    vmcb->iopm_base_pa = io_bitmap_pa;
    vmcb->icept_io = 1;

    msr_bitmap_pa = pmm::alloc_n_blocks(msr_bitmap_size);
    msr_bitmap = (uint8_t*)(msr_bitmap_pa + phys_mem_map);

    memset(msr_bitmap, 0xFF, msr_bitmap_size * pmm::block_size);
    vmcb->msrpm_base_pa = msr_bitmap_pa;
    vmcb->icept_msr = 1;
}

svm::Vm::~Vm() {
    pmm::free_block(vmcb_pa);
    for(size_t i = 0; i < io_bitmap_size; i++)
        pmm::free_block(io_bitmap_pa + (i * pmm::block_size));

    for(size_t i = 0; i < msr_bitmap_size; i++)
        pmm::free_block(msr_bitmap_pa + (i * pmm::block_size));
}

extern "C" void svm_vmrun(svm::GprState* guest_gprs, uint64_t vmcb_pa);

void svm::Vm::inject_int(vm::AbstractVm::InjectType type, uint8_t vector, bool error_code, uint32_t error) {
    uint8_t type_val = 0;
    switch (type) {
        case vm::AbstractVm::InjectType::ExtInt: type_val = 0; break;
        case vm::AbstractVm::InjectType::NMI: type_val = 2; break;
        case vm::AbstractVm::InjectType::Exception: type_val = 3; break;
        case vm::AbstractVm::InjectType::SoftwareInt: type_val = 4; break;
    }
    uint64_t v = 0;
    v |= vector; // Vector
    v |= (type_val << 8); // Type
    v |= (error_code << 11);
    v |= (1ull << 31); // Valid

    if(error_code) 
        v |= ((uint64_t)error << 32);

    vmcb->event_inject = v; // Is cleared upon VMEXIT
}

void svm::Vm::set(vm::VmCap cap, bool value) {
    if(cap == vm::VmCap::FullPIOAccess)
        vmcb->icept_io = (value ? 0 : 1);
}

bool svm::Vm::run(vm::VmExit& exit) {
    while(true) {
        asm("clgi");

        auto fs_base = msr::read(msr::fs_base);
        auto gs_base = msr::read(msr::gs_base);
        auto kgs_base = msr::read(msr::kernel_gs_base);
        auto pat = msr::read(msr::ia32_pat);

        vmcb->tsc_offset = -cpu::rdtsc() + vcpu->ia32_tsc;

        host_simd.store();
        guest_simd.load();

        svm_vmrun(&guest_gprs, vmcb_pa);

        msr::write(msr::fs_base, fs_base);
        msr::write(msr::gs_base, gs_base);
        msr::write(msr::kernel_gs_base, kgs_base);
        msr::write(msr::ia32_pat, pat);

        guest_simd.store();
        host_simd.load();

        vcpu->ia32_tsc = cpu::rdtsc() + vmcb->tsc_offset;

        auto& cpu_data = get_cpu();
        cpu_data.tss_table.load(cpu_data.gdt_table.push_tss(&cpu_data.tss_table, cpu_data.tss_sel));

        asm("stgi");

        auto next_instruction = [&]() { vmcb->rip += exit.instruction_len; };

        auto code = vmcb->exitcode;
        switch (code) {
        case 0x40 ... 0x5F: { // Exception
            auto int_no = code - 0x40;
            auto grip = vmcb->cs.base + vmcb->rip;

            if(int_no == 6) { // #UD
                uint8_t instruction[15];
                vcpu->mem_read(grip, {instruction});

                // Make sure `VMCALL` from intel also works
                if(instruction[0] == 0x0F && instruction[1] == 0x01 && instruction[2] == 0xC1) {
                    exit.reason = vm::VmExit::Reason::Vmcall;

                    exit.instruction_len = 3;
                    exit.instruction[0] = 0x0F;
                    exit.instruction[1] = 0x01;
                    exit.instruction[2] = 0xC1;

                    next_instruction();

                    return true;
                }
            }

            print("svm: Interrupt V: {:#x}, gRIP: {:#x}\n", int_no, grip);
            return false;
        }
        case 0x60: // External Interrupt
            break;

        case 0x72: { // CPUID
            exit.reason = vm::VmExit::Reason::CPUID;

            exit.instruction_len = 2;
            exit.instruction[0] = 0x0F;
            exit.instruction[1] = 0xA2;

            next_instruction();

            return true;
        }

        case 0x73: { // RSM
            exit.reason = vm::VmExit::Reason::RSM;

            exit.instruction_len = 2;
            exit.instruction[0] = 0x0F;
            exit.instruction[1] = 0xAA;

            next_instruction();

            return true;
        }

        case 0x7B: { // Port IO
            IOInterceptInfo info{.raw = vmcb->exitinfo1};

            exit.reason = vm::VmExit::Reason::PIO;

            exit.instruction_len = vmcb->exitinfo2 - vmcb->rip;

            exit.pio.address_size = info.address_size;
            exit.pio.segment_index = info.segment;
            exit.pio.size = info.operand_size;
            exit.pio.port = info.port;
            exit.pio.rep = info.rep;
            exit.pio.string = info.string;
            exit.pio.write = !info.dir;

            vmcb->rip = vmcb->exitinfo2;

            return true;
        }

        case 0x7C: { // MSR
            exit.reason = vm::VmExit::Reason::MSR;

            exit.msr.write = vmcb->exitinfo1 & 1;
            exit.instruction_len = 2; // Both WRMSR and RDMSR are 2 bytes long

            exit.instruction[0] = 0x0F;
            exit.instruction[1] = exit.msr.write ? 0x30 : 0x32;

            next_instruction();

            return true;
        }

        case 0x81: { // VMMCALL
            exit.reason = vm::VmExit::Reason::Vmcall;

            exit.instruction_len = 3;
            exit.instruction[0] = 0x0F;
            exit.instruction[1] = 0x01;
            exit.instruction[2] = 0xD9;

            next_instruction();

            return true;
        }
        
        case 0x400: { // Nested Page Fault
            auto addr = vmcb->exitinfo2;
            NPTViolationInfo info{.raw = vmcb->exitinfo1};

            exit.reason = vm::VmExit::Reason::MMUViolation;

            exit.mmu.access.r = !info.write;
            exit.mmu.access.w = info.write;
            exit.mmu.access.x = info.execute;
            exit.mmu.access.user = info.user;

            auto page = static_cast<npt::context*>(mm)->get_page(addr); // This downcast should be safe

            exit.mmu.page.present = info.present;
            exit.mmu.page.r = page.present;
            exit.mmu.page.w = page.writeable;
            exit.mmu.page.x = !page.no_execute;
            exit.mmu.page.user = page.user;

            exit.mmu.gpa = addr;
            exit.mmu.reserved_bits_set = info.reserved_bit_set;

            return true;
        }

        
        default:
            (void)exit;
            print("svm: Unknown exitcode {:#x}\n", code);
            PANIC("SVM unknown exitcode");
        }
    }
}

void svm::Vm::get_regs(vm::RegisterState& regs, uint64_t flags) const {
    if(flags & vm::VmRegs::General) {
        regs.rax = vmcb->rax;

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

        regs.rsp = vmcb->rsp;
        regs.rip = vmcb->rip;
        regs.rflags = vmcb->rflags;

        regs.dr0 = guest_gprs.dr0;
        regs.dr1 = guest_gprs.dr1;
        regs.dr2 = guest_gprs.dr2;
        regs.dr3 = guest_gprs.dr3;
        regs.dr6 = vmcb->dr6;
        regs.dr7 = vmcb->dr7;
    }
    
    if(flags & vm::VmRegs::Control) {
        regs.cr0 = vmcb->cr0;
        regs.cr3 = vmcb->cr3;
        regs.cr4 = vmcb->cr4;

        regs.efer = vmcb->efer;

        regs.sysenter_cs = vmcb->sysenter_cs;
        regs.sysenter_eip = vmcb->sysenter_eip;
        regs.sysenter_esp = vmcb->sysenter_esp;
    }
    
    if(flags & vm::VmRegs::Segment) {
        #define GET_TABLE(table) \
            regs.table.base = vmcb->table.base; \
            regs.table.limit = vmcb->table.limit

        GET_TABLE(gdtr);
        GET_TABLE(idtr);

        #define GET_SEGMENT(segment) \
            regs.segment.base = vmcb->segment.base; \
            regs.segment.limit = vmcb->segment.limit; \
            regs.segment.selector = vmcb->segment.selector; \
            regs.segment.attrib.type = vmcb->segment.attrib & 0xF; \
            regs.segment.attrib.s = (vmcb->segment.attrib >> 4) & 1; \
            regs.segment.attrib.dpl = (vmcb->segment.attrib >> 5) & 0b11; \
            regs.segment.attrib.present = (vmcb->segment.attrib >> 7) & 1; \
            regs.segment.attrib.avl = (vmcb->segment.attrib >> 8) & 1; \
            regs.segment.attrib.l = (vmcb->segment.attrib >> 9) & 1; \
            regs.segment.attrib.db = (vmcb->segment.attrib >> 10) & 1; \
            regs.segment.attrib.g = (vmcb->segment.attrib >> 11) & 1

        GET_SEGMENT(cs);
        GET_SEGMENT(ds);
        GET_SEGMENT(ss);
        GET_SEGMENT(es);
        GET_SEGMENT(fs);
        GET_SEGMENT(gs);

        GET_SEGMENT(ldtr);
        GET_SEGMENT(tr);
    }
}

void svm::Vm::set_regs(const vm::RegisterState& regs, uint64_t flags) {
    if(flags & vm::VmRegs::General) {
        vmcb->rax = regs.rax;

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

        vmcb->rsp = regs.rsp;
        vmcb->rip = regs.rip;
        vmcb->rflags = regs.rflags;

        guest_gprs.dr0 = regs.dr0;
        guest_gprs.dr1 = regs.dr1;
        guest_gprs.dr2 = regs.dr2;
        guest_gprs.dr3 = regs.dr3;
        vmcb->dr6 = regs.dr6;
        vmcb->dr7 = regs.dr7;
    }
    
    if(flags & vm::VmRegs::Control) {
        vmcb->cr0 = regs.cr0;
        vmcb->cr3 = regs.cr3;
        vmcb->cr4 = regs.cr4;

        vmcb->efer = regs.efer;

        vmcb->sysenter_cs = regs.sysenter_cs;
        vmcb->sysenter_eip = regs.sysenter_eip;
        vmcb->sysenter_esp = regs.sysenter_esp;
    }
    
    if(flags & vm::VmRegs::Segment) {
        #define SET_TABLE(table) \
            vmcb->table.base = regs.table.base; \
            vmcb->table.limit = regs.table.limit

        SET_TABLE(gdtr);
        SET_TABLE(idtr);

        #define SET_SEGMENT(segment) \
            vmcb->segment.base = regs.segment.base; \
            vmcb->segment.limit = regs.segment.limit; \
            vmcb->segment.selector = regs.segment.selector; \
            vmcb->segment.attrib = regs.segment.attrib.type | (regs.segment.attrib.s << 4) | \
                                   (regs.segment.attrib.dpl << 5) | (regs.segment.attrib.present << 7) | \
                                   (regs.segment.attrib.avl << 8) | (regs.segment.attrib.l << 9) | \
                                   (regs.segment.attrib.db << 10) | (regs.segment.attrib.g << 11)

        SET_SEGMENT(cs);
        SET_SEGMENT(ds);
        SET_SEGMENT(ss);
        SET_SEGMENT(es);
        SET_SEGMENT(fs);
        SET_SEGMENT(gs);

        SET_SEGMENT(ldtr);
        SET_SEGMENT(tr);
    }
}