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
    bool cpuid(uint32_t leaf, uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d);
    bool cpuid(uint32_t leaf, uint32_t subleaf, uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d);

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
            uint64_t period_ns, period_ms;
        } tsc;

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