#pragma once

#include <Luna/common.hpp>

struct TimePoint {
    static constexpr uint64_t nano_per_secs = 1'000'000'000ull;
    static constexpr uint64_t nano_per_milli = 1'000'000ull;
    static constexpr uint64_t nano_per_micro = 1'000ull;

    static constexpr uint64_t micro_per_secs = 1'000'000ull;
    static constexpr uint64_t micro_per_milli = 1'000ull;

    static constexpr TimePoint from_s(uint64_t s) { return TimePoint{s * nano_per_secs}; }
    static constexpr TimePoint from_ms(uint64_t ms) { return TimePoint{ms * nano_per_milli}; }
    static constexpr TimePoint from_us(uint64_t us) { return TimePoint{us * nano_per_micro}; }
    static constexpr TimePoint from_ns(uint64_t ns) { return TimePoint{ns}; }

    constexpr TimePoint(): time{0} {}

    constexpr uint64_t ns() const { return time; }
    constexpr uint64_t ms() const { return time / nano_per_milli; }
        
    private:
    constexpr TimePoint(uint64_t ns): time{ns} { }
    uint64_t time; // ns
};

constexpr TimePoint operator"" _s(unsigned long long x) { return TimePoint::from_s(x); }
constexpr TimePoint operator"" _ms(unsigned long long x) { return TimePoint::from_ms(x); }
constexpr TimePoint operator"" _us(unsigned long long x) { return TimePoint::from_us(x); }
constexpr TimePoint operator"" _ns(unsigned long long x) { return TimePoint::from_ns(x); }