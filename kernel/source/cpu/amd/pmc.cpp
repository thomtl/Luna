#include <Luna/cpu/amd/pmc.hpp>
#include <Luna/cpu/cpu.hpp>

#include <Luna/misc/log.hpp>

static uint16_t event_cycles_not_in_halt = 0;

void amd_pmc::init() {
    uint32_t a, b, c, d;
    ASSERT(cpu::cpuid(0x8000'0001, a, b, c, d));

    if(!(c & (1 << 23))) // PerfCtrExtCore
        return;

    auto& cpu = get_cpu();

    if(cpu.cpu.family == 0x17 && cpu.cpu.model == 0x18) {
        event_cycles_not_in_halt = 0x76;
    } else {
        print("amd_pmc: Unknown CPU Family / Model\n");
        return;
    }

    cpu.cpu.pmc.present = true;
    cpu.cpu.pmc.vendor = CpuVendor::AMD;

    return;
}

void amd_pmc::enable_counter(uint64_t interval) {
    uint64_t cmd = (1ull << 41) | // Only count host events
                   (((((uint64_t)event_cycles_not_in_halt) >> 8) & 0xF) << 32) | // Event select 11:8
                   (1ull << 20) | // Enable IRQs
                   (1ull << 17) | // Only count in Ring 0
                   ((uint64_t)event_cycles_not_in_halt & 0xFF); // Event select 7:0

    msr::write(msr::perf_evt_sel0, cmd);

    msr::write(msr::perf_ctr_0, -interval);

    msr::write(msr::perf_evt_sel0, cmd | (1ull << 22)); // Enable
}

bool amd_pmc::counter_did_overflow() {
    return !(msr::read(msr::perf_ctr_0) & (1ull << 47));
}