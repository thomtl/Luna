#pragma once

#include <Luna/common.hpp>
#include <Luna/misc/time.hpp>

namespace tsc {
    void init_per_cpu();

    uint64_t rdtsc();

    uint64_t time_ns();
    uint64_t time_ns_at(uint64_t count);

    void poll_sleep(const TimePoint& duration);
} // namespace tsc
