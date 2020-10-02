#include <Luna/cpu/regs.hpp>

uint64_t msr::read(uint32_t msr) {
    uint64_t high = 0, low = 0;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return (high << 32) | low;
}

void msr::write(uint32_t msr, uint64_t v) {
    asm volatile("wrmsr" : : "a"(v & 0xFFFFFFFF), "d"(v >> 32), "c"(msr));
}

uint64_t cr4::read(){
    uint64_t ret = 0;
    asm volatile("mov %%cr4, %0" : "=r"(ret));

    return ret;
}

void cr4::write(uint64_t v){
    asm volatile("mov %0, %%cr4" : : "r"(v) : "memory");
}