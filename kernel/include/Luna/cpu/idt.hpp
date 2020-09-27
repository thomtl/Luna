#pragma once

#include <Luna/common.hpp>
#include <Luna/misc/format.hpp>

namespace idt
{
    struct [[gnu::packed]] regs {
        uint64_t ds;
        uint64_t r15, r14, r13, r12, r11, r10, r9, r8, rbp, useless, rsi, rdi, rdx, rcx, rbx, rax;
        uint64_t int_num, error_code;
        uint64_t rip, cs, rflags, rsp, ss;
    };

    struct [[gnu::packed]] pointer {
        uint16_t size;
        uint64_t table;

        void set() {
            asm volatile("lidt %0" : : "m"(*this) : "memory");
        }
    };

    struct [[gnu::packed]] entry {
        uint16_t function_low;
        uint16_t code_seg;
        uint16_t flags;
        uint16_t function_mid;
        uint32_t function_high;
        uint32_t reserved;

        entry() = default;
        constexpr entry(uintptr_t f, uint16_t seg, uint8_t ist = 0, uint8_t dpl = 0) :  function_low{(uint16_t)(f & 0xFFFF)},
                                                                                        code_seg{seg},
                                                                                        flags{(uint16_t)((ist & 0x7) | (7 << 9) | ((dpl & 0x3) << 13) | (1 << 15))},
                                                                                        function_mid{(uint16_t)((f >> 16) & 0xFFFF)},
                                                                                        function_high{(uint32_t)((f >> 32) & 0xFFFFFFFF)},
                                                                                        reserved{0} {
            
                                                                                        }
    };
    static_assert(sizeof(entry) == 16);

    constexpr size_t n_table_entries = 256;

    void init_table();
    void load();
} // namespace idt
