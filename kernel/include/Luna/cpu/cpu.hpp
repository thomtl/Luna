#pragma once

#include <Luna/common.hpp>
#include <Luna/cpu/regs.hpp>
#include <Luna/cpu/gdt.hpp>

struct CpuData {
    void* self;
    uint32_t lapic_id;
    gdt::table gdt_table;

    void set();
};

CpuData& get_cpu();