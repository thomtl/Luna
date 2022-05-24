#pragma once

#include <Luna/common.hpp>
#include <Luna/cpu/gdt.hpp>
#include <Luna/cpu/lapic.hpp>
#include <Luna/cpu/stack.hpp>

#include <Luna/cpu/amd/asid.hpp>

#include <std/utility.hpp>

namespace threading {
    struct Thread;
} // namespace threading


namespace cpu {
    constexpr uint32_t signature_intel_ebx = 0x756e6547;
    constexpr uint32_t signature_intel_edx = 0x49656e69;
    constexpr uint32_t signature_intel_ecx = 0x6c65746e;


    bool cpuid(uint32_t leaf, uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d);
    bool cpuid(uint32_t leaf, uint32_t subleaf, uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d);

    uint64_t rdtsc();

    void early_init(); // Cannot access per_cpu struct
    void init(); // Can access per_cpu struct

    void cache_flush(void* addr, size_t size);
} // namespace cpu

enum class CpuVendor { Unknown, AMD, Intel };

struct CpuData {
    CpuData() = default;
    CpuData(const CpuData& b) = delete;
    CpuData& operator=(const CpuData&) = delete;

    void* self;
    uint32_t lapic_id;
    gdt::table gdt_table;
    tss::Table tss_table;
    lapic::Lapic lapic;

    uint16_t tss_sel;

    threading::Thread* current_thread;
    std::lazy_initializer<cpu::Stack> thread_kill_stack; // Initialized in threading init code

    struct {
        uint16_t family;
        uint8_t model;
        uint8_t stepping;

        struct {
            void (*flush)(uintptr_t ptr);

            size_t clflush_size;
        } cache;

        struct {
            CpuVendor vendor;
        } vm;

        struct {
            uint8_t ept_levels;
            bool ept_dirty_accessed;
        } vmx;

        struct {
            uint32_t n_asids;
            std::lazy_initializer<svm::AsidManager> asid_manager;
        } svm;
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
    volatile uint64_t serving;
    volatile uint64_t next_ticket;
};

struct IrqTicketLock {
    constexpr IrqTicketLock(): saved_if{false}, _lock{} {}

    void lock() {
        uint64_t rflags = 0;
        asm volatile("pushfq\r\npop %0" : "=r"(rflags));
        bool tmp_if = (rflags >> 9) & 1;

        asm volatile("cli");
        _lock.lock();

        saved_if = tmp_if;
    }

    void unlock() {
        bool tmp_if = saved_if;
        _lock.unlock();

        if(tmp_if)
            asm volatile("sti");
        else
            asm volatile("cli");
    }

    bool saved_if;
    private:
    TicketLock _lock;
};