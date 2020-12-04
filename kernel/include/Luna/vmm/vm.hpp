#pragma once

#include <Luna/common.hpp>

namespace vm {
    struct RegisterState {
        uint64_t rax, rbx, rcx, rdx, rdi, rsi, rbp;
        uint64_t r8, r9, r10, r11, r12, r13, r14, r15;

        uint64_t rsp, rip, rflags;

        struct Segment {
            uint16_t selector;
            uint64_t base;
            uint32_t limit;
            uint16_t attrib;
        };
        Segment cs, ds, ss, es, fs, gs, ldtr, tr;

        struct Table {
            uint64_t base;
            uint16_t limit;
        };
        Table gdtr, idtr;

        uint64_t cr0, cr3, cr4;
        uint64_t efer;
        uint64_t dr7;
    };

    constexpr size_t max_x86_instruction_size = 15;
    struct VmExit {
        enum class Reason { Hlt };
        static constexpr const char* reason_to_string(const Reason& reason) {
            switch (reason) {
                case Reason::Hlt: return "HLT";
                default: return "Unknown";
            }
        }

        Reason reason;

        uint8_t instruction_len;
        uint8_t instruction[max_x86_instruction_size];
    };

    struct AbstractVm {
        virtual ~AbstractVm() {}

        virtual void get_regs(vm::RegisterState& regs) const = 0;
        virtual void set_regs(const vm::RegisterState& regs) = 0;

        virtual void map(uintptr_t hpa, uintptr_t gpa, uint64_t flags) = 0;

        virtual bool run(VmExit& exit) = 0;
    };

    struct Vm {
        Vm();
        
        void get_regs(vm::RegisterState& regs) const;
        void set_regs(const vm::RegisterState& regs);

        void map(uintptr_t hpa, uintptr_t gpa, uint64_t flags);
        bool run(VmExit& exit);

        private:
        AbstractVm* vm;
    };

    void init();
} // namespace vm
