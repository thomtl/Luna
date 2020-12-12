#include <Luna/cpu/cpu.hpp>
#include <Luna/cpu/regs.hpp>
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

void cpu::early_init() {
    // NX Support
    {
        uint32_t a, b, c, d;
        ASSERT(cpuid(0x8000'0001, a, b, c, d));

        ASSERT(d & (1 << 20));

        msr::write(msr::ia32_efer, msr::read(msr::ia32_efer) | (1 << 11)); // Enable No-Execute Support
    }

    // Enable Write-Protect in Ring 0
    cr0::write(cr0::read() | (1 << 16));
}

void cpu::init() {
    auto& cpu_data = get_cpu();
    {
        uint32_t a, b, c, d;
        ASSERT(cpuid(0x1, a, b, c, d));

        uint8_t ext_family = (a >> 20) & 0xFF;
        uint8_t basic_family = (a >> 8) & 0xFF;

        cpu_data.cpu.family = basic_family + ((basic_family == 15) ? ext_family : 0);

        uint8_t ext_model = (a >> 16) & 0xF;
        uint8_t basic_model = (a >> 4) & 0xF;

        if(basic_family == 6 || basic_family == 15)
            cpu_data.cpu.model = (ext_model << 4) | basic_model;
        else
            cpu_data.cpu.model = basic_model;

        cpu_data.cpu.stepping = a & 0xF;
    }

    {
        uint32_t a, b, c, d;
        ASSERT(cpuid(1, a, b, c, d));
        cpu_data.cpu.cache.clflush_size = ((b >> 8) & 0xFF) * 8;

        if(d & (1 << 19))
            cpu_data.cpu.cache.clflush = true;

        ASSERT(cpuid(7, 0, a, b, c, d));

        if(b & (1 << 23))
            cpu_data.cpu.cache.clflushopt = true;
    }
}

void cpu::cache_flush(void* addr, size_t size) {
    auto& cpu = get_cpu().cpu;
    if(cpu.cache.clflushopt) {
        uintptr_t base = ((uintptr_t)addr & ~(cpu.cache.clflush_size - 1));
        uintptr_t top = ((uintptr_t)addr) + size;

        asm volatile("mfence" : : : "memory");

        for(; base < top; base += cpu.cache.clflush_size)
            asm volatile("clflushopt (%0)" : : "r"(base) : "memory");

        asm volatile("mfence" : : : "memory");
    } else if(cpu.cache.clflush) {
        uintptr_t base = ((uintptr_t)addr & ~(cpu.cache.clflush_size - 1));
        uintptr_t top = ((uintptr_t)addr) + size;

        for(; base < top; base += cpu.cache.clflush_size)
            asm volatile("clflush (%0)" : : "r"(base) : "memory");
    } else {
        PANIC("No known cache flush mechanism");
    }
}