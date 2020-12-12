#pragma once

#include <Luna/common.hpp>
#include <Luna/cpu/gdt.hpp>
#include <Luna/cpu/lapic.hpp>

namespace cpu {
    constexpr uint32_t signature_intel_ebx = 0x756e6547;
    constexpr uint32_t signature_intel_edx = 0x49656e69;
    constexpr uint32_t signature_intel_ecx = 0x6c65746e;


    bool cpuid(uint32_t leaf, uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d);
    bool cpuid(uint32_t leaf, uint32_t subleaf, uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d);

    void early_init(); // Cannot access per_cpu struct
    void init(); // Can access per_cpu struct

    void cache_flush(void* addr, size_t size);
} // namespace cpu

struct CpuData {
    void* self;
    uint32_t lapic_id;
    gdt::table gdt_table;
    tss::Table tss_table;
    lapic::Lapic lapic;

    struct {
        uint16_t family;
        uint8_t model;
        uint8_t stepping;

        struct {
            bool clflush, clflushopt;

            size_t clflush_size;
        } cache;

        struct {
            uint8_t ept_levels;
            bool ept_dirty_accessed;
        } vmx;
    } cpu;

    struct {
        size_t region_size, region_alignment;
        void (*store)(uint8_t* context);
        void (*load)(const uint8_t* context);
    } simd_data;

    void set();
};

CpuData& get_cpu();

struct TicketLock {
    constexpr TicketLock(): serving{0}, next_ticket{0} {}

    void lock() {
        auto ticket = __atomic_fetch_add(&next_ticket, 1, __ATOMIC_SEQ_CST);
        while(__atomic_load_n(&serving, __ATOMIC_SEQ_CST) != ticket)
            asm("pause");
    }

    void unlock() {
        __atomic_add_fetch(&serving, 1, __ATOMIC_SEQ_CST);
    }

    private:
    uint64_t serving;
    uint64_t next_ticket;
};