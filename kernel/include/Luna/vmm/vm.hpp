#pragma once

#include <Luna/common.hpp>

namespace vm {
    struct RegisterState {
        uint64_t rax, rbx, rcx, rdx, rdi, rsi, rbp;
        uint64_t r8, r9, r10, r11, r12, r13, r14, r15;

        uint64_t rsp, rip, rflags;

        struct Segment {
            uint64_t base;
            uint32_t limit;
            uint16_t attrib, selector;
        };
        Segment cs, ds, ss, es, fs, gs, ldtr, tr;

        struct Table {
            uint64_t base;
            uint16_t limit;
        };
        Table gdtr, idtr;

        uint64_t cr0, cr3, cr4;
        uint64_t efer;
    };

    struct AbstractVm {
        virtual ~AbstractVm() {}

        virtual void get_regs(vm::RegisterState& regs) const = 0;
        virtual void set_regs(const vm::RegisterState& regs) = 0;

        virtual void map(uintptr_t hpa, uintptr_t gpa, uint64_t flags) = 0;

        virtual void run() = 0;
    };
} // namespace vm
