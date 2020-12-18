#pragma once

#include <Luna/common.hpp>

namespace msr {
    constexpr uint32_t apic_base = 0x1B;
    constexpr uint32_t ia32_feature_control = 0x3A;
    constexpr uint32_t ia32_pat = 0x277;

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

    constexpr uint32_t ia32_efer = 0xC0000080;
    constexpr uint32_t fs_base = 0xC0000100;
    constexpr uint32_t gs_base = 0xC0000101;
    constexpr uint32_t kernel_gs_base = 0xC0000102;

    constexpr uint32_t vm_cr = 0xC0010114;
    constexpr uint32_t vm_hsave_pa = 0xC0010117;

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

        void store();
        void load() const;
        FxState* data() { return (FxState*)_ctx; }

        private:
        uint8_t* _ctx;
    };
} // namespace simd
