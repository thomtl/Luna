#pragma once

#include <Luna/common.hpp>
#include <Luna/cpu/regs.hpp>

namespace amd_pmc {
    void init();

    void enable_counter(uint64_t interval);
    bool counter_did_overflow();
} // namespace amd_pmc
