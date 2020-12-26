#include <Luna/cpu/lapic.hpp>
#include <Luna/cpu/regs.hpp>
#include <Luna/cpu/cpu.hpp>

#include <Luna/mm/vmm.hpp>

uint32_t lapic::Lapic::read(uint32_t reg) {
    if(x2apic)
        return msr::read(msr::x2apic_base + (reg >> 4));
    else
        return *(volatile uint32_t*)(mmio_base + reg);
}

void lapic::Lapic::write(uint32_t reg, uint32_t v) {
    if(x2apic)
        return msr::write(msr::x2apic_base + (reg >> 4), v);
    else
        *(volatile uint32_t*)(mmio_base + reg) = v;
}

#include <Luna/misc/format.hpp>

void lapic::Lapic::init() {
    uint32_t a, b, c, d;
    ASSERT(cpu::cpuid(1, a, b, c, d));

    x2apic = ((c & (1 << 21)) != 0);

    auto base = msr::read(msr::ia32_apic_base);
    base |= (1 << 11); // Set Enable bit
    base |= (x2apic << 10); // Set x2APIC bit if supported
    msr::write(msr::ia32_apic_base, base);

    auto mmio_base_pa = base & 0xFFFF'FFFF'FFFF'F000;
    mmio_base = mmio_base_pa + phys_mem_map;

    if(!x2apic)
        vmm::kernel_vmm::get_instance().map(mmio_base_pa, mmio_base, paging::mapPagePresent | paging::mapPageWrite);

    uint32_t id = 0;
    if(x2apic)
        id = read(regs::id);
    else
        id = (read(regs::id) >> 24) & 0xFF;

    get_cpu().lapic_id = id;

    write(regs::tpr, 0); // Enable all interrupt classes
    write(regs::spurious, 0xFF | (1 << 8)); // Spurious IRQ is 0xFF and enable the LAPIC

    ticks_per_ms = 0;
}

void lapic::Lapic::eoi() {
    write(regs::eoi, 0);
}

void lapic::Lapic::start_timer(uint8_t vector, uint64_t ms, lapic::regs::LapicTimerModes mode, void (*poll)(uint64_t ms)) {
    if(ticks_per_ms == 0) {
        write(regs::timer_divider, 3);
        write(regs::timer_initial_count, ~0);

        write(regs::lvt_timer, read(regs::lvt_timer) & ~(1 << 16)); // Clear timer mask
        poll(10);
        write(regs::lvt_timer, read(regs::lvt_timer) | (1 << 16)); // Set timer mask

        ticks_per_ms = (~0 - read(regs::timer_current_count)) / 10;
    }

    write(regs::timer_divider, 3);
    write(regs::lvt_timer, (read(regs::lvt_timer) & ~(0b11 << 17)) | ((uint8_t)mode << 17));
    write(regs::lvt_timer, (read(regs::lvt_timer) & 0xFFFFFF00) | vector);
    write(regs::timer_initial_count, ticks_per_ms * ms);
    write(regs::lvt_timer, read(regs::lvt_timer) & ~(1 << 16)); // Clear timer mask
}