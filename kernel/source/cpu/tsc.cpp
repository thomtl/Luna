#include <Luna/cpu/tsc.hpp>

#include <Luna/cpu/cpu.hpp>
#include <Luna/drivers/timers/hpet.hpp>
#include <Luna/misc/log.hpp>

constexpr uint64_t nano_per_milli = 1'000'000ull;

uint64_t tsc::rdtsc() {
    uint32_t a, d;
    asm volatile("lfence\nrdtsc" : "=a"(a), "=d"(d) : : "memory");

    return a | ((uint64_t)d << 32);
}

void tsc::poll_msleep(size_t ms) {
    auto delta = ms * get_cpu().cpu.tsc.period_ms;
    auto goal = rdtsc() + delta;

    while(rdtsc() < goal)
        asm("pause");
}

void tsc::poll_nsleep(size_t ns) {
    auto delta = ns * get_cpu().cpu.tsc.period_ns;
    auto goal = rdtsc() + delta;

    while(rdtsc() < goal)
        asm("pause");
}

uint64_t tsc::time_ns() {
    return rdtsc() / get_cpu().cpu.tsc.period_ns;
}

uint64_t tsc::time_ns_at(uint64_t count) {
    return count / get_cpu().cpu.tsc.period_ns;
}


void tsc::init_per_cpu() {
    uint32_t a, b, c, d;
    ASSERT(cpu::cpuid(0x8000'0007, a, b, c, d));
    ASSERT(d & (1 << 8)); // Invariant TSC
    
    constexpr size_t calibration_time = 10; // ms

    IrqTicketLock lock{};
    {
        std::lock_guard guard{lock}; // Disable IRQs

        auto start = rdtsc();
        hpet::poll_msleep(calibration_time);
        auto end = rdtsc();


        auto& info = get_cpu().cpu.tsc;
        info.period_ms = (end - start) / calibration_time;
        info.period_ns = info.period_ms / nano_per_milli;
    }

    //print("tsc: Frequency: {}.{} MHz\n", ticks_per_ms / 1000, ticks_per_ms % 1000);
    //print("tsc: {} /ns {} /ms", get_cpu().cpu.tsc.period_ns, get_cpu().cpu.tsc.period_ms);
}