#pragma once

#include <Luna/common.hpp>

namespace gdt
{
    struct [[gnu::packed]] pointer {
        uint16_t size;
        uint64_t table;

        void set() {
            asm volatile("lgdt %0" : : "m"(*this) : "memory");
        }
    };

    constexpr uint16_t kcode = 0x8;
    constexpr uint16_t kdata = 0x10;

    struct table {
        void init(){
            entries[i++] = 0;

            // Executable, Descriptor, Present, 64bit
            entries[i++] = (1ull << 43) | (1ull << 44) | (1ull << 47) | (1ull << 53); // Kernel Code 

            // Read Write, Descriptor, Present
            entries[i++] = (1ull << 41ull) | (1ull << 44) | (1ull << 47); // Kernel Data 

            set();
        }

        void set(){
            pointer p{.size = (table_entries * sizeof(uint64_t)) - 1, .table = (uint64_t)&entries};
            p.set();

            asm volatile (R"(
                    mov %%rsp, %%rax
                    push $0x10
                    push %%rax
                    pushf
                    push $0x8
                    push $1f
                    iretq
                    1:
                    mov $0x10, %%ax
                    mov %%ax, %%ds
                    mov %%ax, %%es
                    mov %%ax, %%ss
                    mov %%ax, %%fs
                    mov %%ax, %%gs
                )" : : : "rax", "memory");
        }

        private:
        static constexpr size_t table_entries = 10;
        uint64_t entries[table_entries];
        size_t i = 0;
    };
} // namespace gdt
