#include <Luna/cpu/regs.hpp>

uint64_t msr::read(uint32_t msr) {
    uint64_t high = 0, low = 0;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return (high << 32) | low;
}

void msr::write(uint32_t msr, uint64_t v) {
    asm volatile("wrmsr" : : "a"(v & 0xFFFFFFFF), "d"(v >> 32), "c"(msr));
}