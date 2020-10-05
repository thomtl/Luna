#pragma once

#include <Luna/common.hpp>

#include <Luna/mm/hmm.hpp>

#include <Luna/misc/stivale2.hpp>

namespace smp {
    void start_cpus(stivale2::Parser& boot_info, void (*f)(stivale2_smp_info*));
} // namespace smp
