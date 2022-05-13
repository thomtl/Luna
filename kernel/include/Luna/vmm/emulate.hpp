#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

namespace vm::emulate {
    enum class r64 { Rax = 0, Rcx, Rdx, Rbx, Rsp, Rbp, Rsi, Rdi };
    enum class sreg { Es = 0, Cs, Ss, Ds, Fs, Gs };

    void emulate_instruction(vm::VCPU* vcpu, uintptr_t gpa, std::pair<uintptr_t, size_t> mmio_region, uint8_t instruction[max_x86_instruction_size], vm::RegisterState& regs, vm::AbstractMMIODriver* driver);

    struct Modrm {
        uint8_t mod, reg, rm;
    };

    constexpr Modrm parse_modrm(uint8_t v) {
        return Modrm{.mod = (uint8_t)((v >> 6) & 0b11), .reg = (uint8_t)((v >> 3) & 0b111), .rm = (uint8_t)(v & 0b111)};
    }

    struct Sib {
        uint8_t scale, index, base;
    };

    constexpr Sib parse_sib(uint8_t v) {
        return Sib{.scale = (uint8_t)((v >> 6) & 0b11), .index = (uint8_t)((v >> 3) & 0b111), .base = (uint8_t)(v & 0b111)};
    }

    uint64_t read_r64(vm::RegisterState& regs, vm::emulate::r64 r, uint8_t s);
    void write_r64(vm::RegisterState& regs, vm::emulate::r64 r, uint64_t v, uint8_t s);
    RegisterState::Segment& get_sreg(RegisterState& regs, sreg r);

} // namespace vm::emulate