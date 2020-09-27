#include <Luna/cpu/cpu.hpp>

void CpuData::set() {
    self = this;
    msr::write(msr::gs_base, (uint64_t)&self);
}

CpuData& get_cpu() {
    uint64_t ret = 0;
    asm volatile("mov %%gs:0, %0" : "=r"(ret));

    return *reinterpret_cast<CpuData*>(ret);
}