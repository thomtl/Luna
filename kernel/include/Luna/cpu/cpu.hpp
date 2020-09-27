#pragma once

#include <Luna/common.hpp>
#include <Luna/cpu/regs.hpp>

struct CpuData {
    uint32_t lapic_id;
    void* self;

    void set();
};

CpuData& get_cpu();