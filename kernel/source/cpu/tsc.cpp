#include <Luna/cpu/tsc.hpp>

#include <Luna/cpu/cpu.hpp>
#include <Luna/drivers/timers/hpet.hpp>

#include <Luna/misc/log.hpp>

uint64_t tsc::rdtsc() {
    uint32_t a, d;
    asm volatile("lfence\nrdtsc" : "=a"(a), "=d"(d) : : "memory");

    return a | ((uint64_t)d << 32);
}

void tsc::poll_sleep(const TimePoint& duration) {
    auto delta = duration.ns() * get_cpu().cpu.tsc.period_ns;
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
    
    constexpr TimePoint calibration_time = 10_ms;

    IrqTicketLock lock{};
    {
        std::lock_guard guard{lock}; // Disable IRQs

        auto start = rdtsc();
        hpet::poll_sleep(calibration_time);
        auto end = rdtsc();


        auto& info = get_cpu().cpu.tsc;
        auto period_ms = (end - start) / calibration_time.ms();
        info.period_ns = period_ms / TimePoint::nano_per_milli;

        //print("tsc: Frequency: {}.{} MHz\n", period_ms / 1000, period_ms % 1000);
        //print("tsc: {} ticks/ns {} ticks/ms\n", info.period_ns, period_ms);
    }
}