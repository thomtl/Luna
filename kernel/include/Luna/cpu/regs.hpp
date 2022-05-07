#pragma once

#include <Luna/common.hpp>

namespace msr {
    constexpr uint32_t ia32_tsc = 0x10;
    constexpr uint32_t ia32_apic_base = 0x1B;
    constexpr uint32_t ia32_feature_control = 0x3A;
    constexpr uint32_t ia32_bios_sign_id = 0x8B;

    constexpr uint32_t ia32_mtrr_cap = 0xFE;

    constexpr uint32_t ia32_arch_capabilities = 0x10A;

    constexpr uint32_t ia32_sysenter_cs = 0x174;
    constexpr uint32_t ia32_sysenter_esp = 0x175;
    constexpr uint32_t ia32_sysenter_eip = 0x176;
    
    constexpr uint32_t ia32_mtrr_physbase0 = 0x200;
    constexpr uint32_t ia32_mtrr_physmask0 = 0x201;
    constexpr uint32_t ia32_mtrr_physbase1 = 0x202;
    constexpr uint32_t ia32_mtrr_physmask1 = 0x203;
    constexpr uint32_t ia32_mtrr_physbase2 = 0x204;
    constexpr uint32_t ia32_mtrr_physmask2 = 0x205;
    constexpr uint32_t ia32_mtrr_physbase3 = 0x206;
    constexpr uint32_t ia32_mtrr_physmask3 = 0x207;
    constexpr uint32_t ia32_mtrr_physbase4 = 0x208;
    constexpr uint32_t ia32_mtrr_physmask4 = 0x209;
    constexpr uint32_t ia32_mtrr_physbase5 = 0x20A;
    constexpr uint32_t ia32_mtrr_physmask5 = 0x20B;
    constexpr uint32_t ia32_mtrr_physbase6 = 0x20C;
    constexpr uint32_t ia32_mtrr_physmask6 = 0x20D;
    constexpr uint32_t ia32_mtrr_physbase7 = 0x20E;
    constexpr uint32_t ia32_mtrr_physmask7 = 0x20F;

    constexpr uint32_t ia32_mtrr_fix64K_00000 = 0x250;
    constexpr uint32_t ia32_mtrr_fix16K_80000 = 0x258;
    constexpr uint32_t ia32_mtrr_fix16K_A0000 = 0x259;
    constexpr uint32_t ia32_mtrr_fix4K_C0000 = 0x268;
    constexpr uint32_t ia32_mtrr_fix4K_C8000 = 0x269;
    constexpr uint32_t ia32_mtrr_fix4K_D0000 = 0x26A;
    constexpr uint32_t ia32_mtrr_fix4K_D8000 = 0x26B;
    constexpr uint32_t ia32_mtrr_fix4K_E0000 = 0x26C;
    constexpr uint32_t ia32_mtrr_fix4K_E8000 = 0x26D;
    constexpr uint32_t ia32_mtrr_fix4K_F0000 = 0x26E;
    constexpr uint32_t ia32_mtrr_fix4K_F8000 = 0x26F;

    constexpr uint32_t ia32_pat = 0x277;
    constexpr uint32_t ia32_mtrr_def_type = 0x2FF;

    constexpr uint32_t ia32_vmx_basic = 0x480;
    constexpr uint32_t ia32_vmx_pinbased_ctls = 0x481;
    constexpr uint32_t ia32_vmx_procbased_ctls = 0x482;
    constexpr uint32_t ia32_vmx_exit_ctls = 0x483;
    constexpr uint32_t ia32_vmx_entry_ctls = 0x484;
    constexpr uint32_t ia32_vmx_misc = 0x485;
    constexpr uint32_t ia32_vmx_cr0_fixed0 = 0x486;
    constexpr uint32_t ia32_vmx_cr0_fixed1 = 0x487;
    constexpr uint32_t ia32_vmx_cr4_fixed0 = 0x488;
    constexpr uint32_t ia32_vmx_cr4_fixed1 = 0x489;
    constexpr uint32_t ia32_vmx_vmcs_enum = 0x48A;
    constexpr uint32_t ia32_vmx_procbased_ctls2 = 0x48B;
    constexpr uint32_t ia32_vmx_ept_vpid_cap = 0x48C;
    constexpr uint32_t ia32_vmx_true_pinbased_ctls = 0x48D;
    constexpr uint32_t ia32_vmx_true_procbased_ctls = 0x48E;
    constexpr uint32_t ia32_vmx_true_exit_ctls = 0x48F;
    constexpr uint32_t ia32_vmx_true_entry_ctls = 0x490;
    constexpr uint32_t ia32_vmx_vmfunc = 0x491;

    constexpr uint32_t x2apic_base = 0x800;

    constexpr uint32_t ia32_xss = 0xDA0;

    constexpr uint32_t ia32_efer = 0xC0000080;
    constexpr uint32_t fs_base = 0xC0000100;
    constexpr uint32_t gs_base = 0xC0000101;
    constexpr uint32_t kernel_gs_base = 0xC0000102;

    constexpr uint32_t syscfg = 0xC0010010;

    constexpr uint32_t vm_cr = 0xC0010114;
    constexpr uint32_t vm_hsave_pa = 0xC0010117;

    constexpr uint32_t osvw_id_length = 0xC0010140;
    constexpr uint32_t osvw_status = 0xC0010141;

    namespace pat {
        constexpr uint64_t uc = 0;
        constexpr uint64_t wc = 1;
        constexpr uint64_t wt = 4;
        constexpr uint64_t wp = 5;
        constexpr uint64_t wb = 6;
        constexpr uint64_t uc_ = 7;

        constexpr uint8_t uncacheable = uc;
        constexpr uint8_t write_combine = wc;
        constexpr uint8_t write_through = wt;
        constexpr uint8_t write_protect = wp;
        constexpr uint8_t write_back = wb;
        constexpr uint8_t uc_minus = uc_;

        constexpr uint64_t default_pat = uc | (wc << 8) | (wt << 32) | (wp << 40) | (wb << 48) | (uc_ << 56);

        constexpr uint64_t reset_state_pat = 0x0007'0406'0007'0406;
    } // namespace pat
    

    uint64_t read(uint32_t msr);
    void write(uint32_t msr, uint64_t v);
} // namespace msr

namespace cr0 {
    uint64_t read();
    void write(uint64_t v);
} // namespace cr0

namespace cr4 {
    uint64_t read();
    void write(uint64_t v);
} // namespace cr4

namespace simd {
    struct [[gnu::packed]] FxState {
        uint16_t fcw, fsw;
        uint8_t ftw, reserved;
        uint16_t fop;
        uint64_t fip, fdp;
        uint32_t mxcsr, mxcsr_mask;

        struct [[gnu::packed]] Mm {
            uint64_t low;
            uint16_t high;
            uint32_t reserved;
            uint16_t reserved_0;
        };
        Mm mm[8];

        struct [[gnu::packed]] Xmm {
            uint64_t low;
            uint64_t high;
        };
        Xmm xmm[16];

        uint8_t reserved_0[48];
        uint8_t available[48];
    };
    static_assert(sizeof(FxState) == 512);

    void init();

    struct Context {
        Context();
        ~Context();

        Context(const Context&) = delete;
        Context(Context&&) = delete;
        Context& operator=(const Context&) = delete;
        Context& operator=(Context&&) = delete;

        void store();
        void load() const;
        FxState* data() { return (FxState*)_ctx; }

        private:
        uint8_t* _ctx;
    };
} // namespace simd
