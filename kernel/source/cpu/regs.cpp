#include <Luna/cpu/regs.hpp>
#include <Luna/cpu/cpu.hpp>

#include <Luna/mm/hmm.hpp>

uint64_t msr::read(uint32_t msr) {
    uint64_t high = 0, low = 0;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return (high << 32) | low;
}

void msr::write(uint32_t msr, uint64_t v) {
    asm volatile("wrmsr" : : "a"(v & 0xFFFFFFFF), "d"(v >> 32), "c"(msr));
}

uint64_t cr0::read(){
    uint64_t ret = 0;
    asm volatile("mov %%cr0, %0" : "=r"(ret));

    return ret;
}

void cr0::write(uint64_t v){
    asm volatile("mov %0, %%cr0" : : "r"(v) : "memory");
}

uint64_t cr4::read(){
    uint64_t ret = 0;
    asm volatile("mov %%cr4, %0" : "=r"(ret));

    return ret;
}

void cr4::write(uint64_t v){
    asm volatile("mov %0, %%cr4" : : "r"(v) : "memory");
}

void simd::init() {
    auto& data = get_cpu().simd_data;

    uint32_t a, b, c, d;
    ASSERT(cpu::cpuid(1, a, b, c, d));

    if(c & (1 << 26)) { // XSAVE
        cr4::write(cr4::read() | (1 << 18)); // Set CR4.OSXSAVE

        ASSERT(cpu::cpuid(0xD, 0, a, b, c, d));

        data.region_size = c;
        data.region_alignment = 64;

        data.load = [](const uint8_t* context) {
            constexpr uint64_t rfbm = ~0ull;

            constexpr uint32_t rfbm_low = rfbm & 0xFFFF'FFFF;
            constexpr uint32_t rfbm_high = (rfbm >> 32) & 0xFFFF'FFFF;

            asm volatile("xrstorq %[Context]" : : [Context] "m"(*context), "a"(rfbm_low), "d"(rfbm_high) : "memory");
        };

        data.store = [](uint8_t* context) {
            constexpr uint64_t rfbm = ~0ull;

            constexpr uint32_t rfbm_low = rfbm & 0xFFFF'FFFF;
            constexpr uint32_t rfbm_high = (rfbm >> 32) & 0xFFFF'FFFF;

            asm volatile("xsaveq %[Context]" : : [Context] "m"(*context), "a"(rfbm_low), "d"(rfbm_high) : "memory");
        };
    } else if(d & (1 << 24)) { // FXSAVE
        cr4::write(cr4::read() | (1 << 9)); // Set CR4.OSFXSR

        data.region_size = 512;
        data.region_alignment = 16;

        data.load = [](const uint8_t* context) {
            asm volatile("fxrstorq %[Context]" : : [Context] "m"(*context) : "memory");
        };

        data.store = [](uint8_t* context) {
            asm volatile("fxsaveq %[Context]" : : [Context] "m"(*context) : "memory");
        };
    } else
        PANIC("No known SIMD Save mechanism"); // FSAVE?? Seriously? What CPU are you running this on
}

simd::Context::Context() {
    _ctx = (uint8_t*)hmm::alloc(get_cpu().simd_data.region_size, get_cpu().simd_data.region_alignment);
    ASSERT(_ctx);
}

simd::Context::~Context() {
    hmm::free((uintptr_t)_ctx);
}

void simd::Context::store() {
    get_cpu().simd_data.store(_ctx);
}

void simd::Context::load() const {
    get_cpu().simd_data.load(_ctx);
}