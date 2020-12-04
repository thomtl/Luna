#pragma once

#include <Luna/common.hpp>
#include <Luna/cpu/tss.hpp>

namespace gdt
{
    struct [[gnu::packed]] pointer {
        uint16_t size;
        uint64_t table;

        void load() const {
            asm volatile("lgdt %0" : : "m"(*this) : "memory");
        }

        void store() {
            asm volatile("sgdt %0" : "=m"(*this) : : "memory");
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
            flush();
        }

        uint16_t push_tss(tss::Table* table) {
            auto ptr = (uintptr_t)table;
            auto size = (sizeof(tss::Table) - 1);

            uint32_t a_low = (size & 0xFFFF) | ((ptr & 0xFFFF) << 16);
            uint32_t a_high = ((ptr >> 16) & 0xFF) | (1 << 8) | (1 << 11) | (1 << 15) | (size & 0xF0000) | (ptr & 0xFF000000);

            uint16_t sel = i * 8;
            entries[i++] = a_low | ((uint64_t)a_high << 32);
            entries[i++] = (ptr >> 32);

            set();

            return sel;
        }

        void set(){
            pointer p{.size = (table_entries * sizeof(uint64_t)) - 1, .table = (uint64_t)&entries};
            p.load();
        }

        void flush() {
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
