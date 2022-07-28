#pragma once

#include <Luna/common.hpp>

namespace tsc {
    void init_per_cpu();

    uint64_t rdtsc();

    uint64_t time_ns();
    uint64_t time_ns_at(uint64_t count);

    void poll_nsleep(uint64_t ns);
    void poll_usleep(uint64_t us);
    void poll_msleep(uint64_t ms);
} // namespace tsc
