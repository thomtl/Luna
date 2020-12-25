#pragma once

#include <Luna/common.hpp>
#include <Luna/cpu/regs.hpp>

#include <Luna/cpu/amd/npt.hpp>
#include <Luna/cpu/paging.hpp>

#include <Luna/vmm/vm.hpp>

namespace svm {
    union [[gnu::packed]] NPTViolationInfo {
        struct {
            uint64_t present : 1;
            uint64_t write : 1;
            uint64_t user : 1;
            uint64_t reserved_bit_set : 1;
            uint64_t execute : 1;
            uint64_t shadow_stack_access : 1;
            uint64_t reserved : 27;

            uint64_t reserved_0 : 32;
        };
        uint64_t raw;
    };
    
    union [[gnu::packed]] IOInterceptInfo {
        struct {
            uint64_t dir : 1;
            uint64_t reserved : 1;
            uint64_t string : 1;
            uint64_t rep : 1;
            uint64_t operand_size : 3;
            uint64_t address_size : 3;
            uint64_t segment : 3;
            uint64_t reserved_1 : 3;
            uint64_t port : 16;
        };
        uint64_t raw;
    };

    constexpr size_t io_bitmap_size = 3;
    constexpr size_t msr_bitmap_size = 2;

    struct [[gnu::packed]] Vmcb {
        uint32_t icept_cr_reads : 16;
        uint32_t icept_cr_writes : 16;

        uint32_t icept_dr_reads : 16;
        uint32_t icept_dr_writes : 16;

        uint32_t icept_exceptions;

        uint32_t icept_intr : 1;
        uint32_t icept_nmi : 1;
        uint32_t icept_smi : 1;
        uint32_t icept_init : 1;
        uint32_t icept_vintr : 1;
        uint32_t icept_cr0_writes : 1;
        uint32_t icept_idtr_reads : 1;
        uint32_t icept_gdtr_reads : 1;
        uint32_t icept_ldtr_reads : 1;
        uint32_t icept_tr_reads : 1;
        uint32_t icept_idtr_writes : 1;
        uint32_t icept_gdtr_writes : 1;
        uint32_t icept_ldtr_writes : 1;
        uint32_t icept_tr_writes : 1;
        uint32_t icept_rdtsc : 1;
        uint32_t icept_rdpmc : 1;
        uint32_t icept_pushf : 1;
        uint32_t icept_popf : 1;
        uint32_t icept_cpuid : 1;
        uint32_t icept_rsm : 1;
        uint32_t icept_iret : 1;
        uint32_t icept_int : 1;
        uint32_t icept_invd : 1;
        uint32_t icept_pause : 1;
        uint32_t icept_hlt : 1;
        uint32_t icept_invlpg : 1;
        uint32_t icept_invlpga : 1;
        uint32_t icept_io : 1;
        uint32_t icept_msr : 1;
        uint32_t icept_task_switch : 1;
        uint32_t ferr_freeze : 1;
        uint32_t icept_shutdown : 1;

        uint32_t icept_vmrun : 1;
        uint32_t icept_vmmcall : 1;
        uint32_t icept_vmload : 1;
        uint32_t icept_vmsave : 1;
        uint32_t icept_stgi : 1;
        uint32_t icept_clgi : 1;
        uint32_t icept_skinit : 1;
        uint32_t icept_rdtscp : 1;
        uint32_t icept_icebp : 1;
        uint32_t icept_wbinvd : 1;
        uint32_t icept_monitor : 1;
        uint32_t icept_mwait_unconditional : 1;
        uint32_t icept_mwait_if_armed : 1;
        uint32_t icept_xsetbv : 1;
        uint32_t icept_rdpru : 1;
        uint32_t icept_efer_write : 1;
        uint32_t icept_cr_writes_after_finish : 16;

        uint8_t reserved[0x28];
        uint16_t pause_filter_threshold;
        uint16_t pause_filter_count;
        uint64_t iopm_base_pa;
        uint64_t msrpm_base_pa;
        uint64_t tsc_offset;

        uint64_t guest_asid : 32;
        uint64_t tlb_control : 8;
        uint64_t reserved_1 : 24;
        
        uint64_t v_tpr : 8;
        uint64_t v_irq : 1;
        uint64_t reserved_2 : 7;
        uint64_t v_intr_priority : 4;
        uint64_t v_ignore_tpr : 1;
        uint64_t reserved_3 : 3;
        uint64_t v_intr_masking : 1;
        uint64_t reserved_4 : 7;
        uint64_t v_intr_vector : 8;
        uint64_t reserved_5 : 24;

        uint64_t irq_shadow : 1;
        uint64_t reserved_6 : 63;

        uint64_t exitcode;
        uint64_t exitinfo1;
        uint64_t exitinfo2;
        uint64_t exitintinfo;

        uint64_t npt_enable : 1;
        uint64_t reserved_7 : 63;

        uint8_t reserved_8[16];
        uint64_t event_inject;
        uint64_t npt_cr3;
        
        uint64_t lbr_enable : 1;
        uint64_t reserved_9 : 63;

        uint32_t vmcb_clean;
        uint32_t reserved_10;
        uint64_t next_rip;
        uint8_t instruction_len;
        uint8_t instruction_bytes[15];
        uint8_t reserved_11[0x320];

        struct [[gnu::packed]] Segment {
            uint16_t selector;
            uint16_t attrib;
            uint32_t limit;
            uint64_t base;
        };
        Segment es, cs, ss, ds, fs, gs;
        Segment gdtr, ldtr, idtr, tr;

        uint8_t reserved_12[0x2B];
        uint8_t cpl;
        uint32_t reserved_13;
        uint64_t efer;
        uint8_t reserved_14[0x70];
        uint64_t cr4, cr3, cr0, dr7, dr6, rflags, rip;
        uint8_t reserved_15[0x58];
        uint64_t rsp;
        uint8_t reserved_16[0x18];
        uint64_t rax, star, lstar, cstar, sfmask, kernel_gs_base, sysenter_cs, sysenter_esp, sysenter_eip;
        uint64_t cr2;
        uint8_t reserved_17[0x20];
        uint64_t pat;
        uint64_t debug_control;
        uint64_t br_from, br_to;
        uint64_t int_from, int_to;
        uint8_t reserved_18[0x968];
    };
    static_assert(sizeof(Vmcb) == 0x1000);

    // ACCESSED FROM ASSEMBLY, DO NOT CHANGE WITHOUT CHANGING svm_low.asm
    struct [[gnu::packed]] GprState {
        uint64_t rbx, rcx, rdx, rsi, rdi, rbp;
        uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
        uint64_t dr0, dr1, dr2, dr3;
    };

    void init();
    bool is_supported();

    uint64_t get_cr0_constraint();
    uint64_t get_efer_constraint();

    struct Vm : public vm::AbstractVm {
        Vm();
        ~Vm();
        bool run(vm::VmExit& exit);

        void get_regs(vm::RegisterState& regs) const;
        void set_regs(const vm::RegisterState& regs);

        void map(uintptr_t hpa, uintptr_t gpa, uint64_t flags) {
            guest_page.map(hpa, gpa, flags | paging::mapPageUser); // NPT walks are always user walks for some reason
        }

        void protect(uintptr_t gpa, uint64_t flags) {
            guest_page.protect(gpa, flags | paging::mapPageUser);
        }

        uintptr_t get_phys(uintptr_t gpa) { return guest_page.get_phys(gpa); }
        simd::Context& get_guest_simd_context() { return guest_simd; }

        void inject_int(vm::AbstractVm::InjectType type, uint8_t vector, bool error_code = false, uint32_t error = 0);

        private:
        uintptr_t vmcb_pa;
        volatile Vmcb* vmcb;

        uint32_t asid;

        simd::Context host_simd, guest_simd;
        npt::context guest_page;
        GprState guest_gprs;

        uint8_t* io_bitmap, *msr_bitmap;
        uintptr_t io_bitmap_pa, msr_bitmap_pa;
    };
} // namespace svm
