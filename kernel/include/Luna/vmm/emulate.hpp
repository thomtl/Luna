#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

namespace vm::emulate {
    enum class r64 { Rax = 0, Rcx, Rdx, Rbx, Rsp, Rbp, Rsi, Rdi };
    enum class sreg { Es = 0, Cs, Ss, Ds, Fs, Gs };

    void emulate_instruction(uint8_t instruction[max_x86_instruction_size], vm::RegisterState& regs, vm::AbstractMMIODriver* driver);
} // namespace vm::emulate