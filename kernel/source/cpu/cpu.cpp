#include <Luna/cpu/cpu.hpp>
#include <cpuid.h>

void CpuData::set() {
    self = this;
    msr::write(msr::gs_base, (uint64_t)&self);
}

CpuData& get_cpu() {
    uint64_t ret = 0;
    asm volatile("mov %%gs:0, %0" : "=r"(ret));

    return *reinterpret_cast<CpuData*>(ret);
}

bool cpu::cpuid(uint32_t leaf, uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d) {
    return __get_cpuid(leaf, &a, &b, &c, &d);
}

bool cpu::cpuid(uint32_t leaf, uint32_t subleaf, uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d) {
    return __get_cpuid_count(leaf, subleaf, &a, &b, &c, &d);
}