#pragma once

#include <Luna/common.hpp>

namespace lapic
{
    namespace regs
    {
        constexpr uint64_t id = 0x20;
        constexpr uint64_t version = 0x30;
        constexpr uint64_t tpr = 0x80;
        constexpr uint64_t apr = 0x90;
        constexpr uint64_t ppr = 0xA0;
        constexpr uint64_t eoi = 0xB0;
        constexpr uint64_t rrd = 0xC0;
        constexpr uint64_t ldr = 0xD0;
        constexpr uint64_t dfr = 0xE0;
        constexpr uint64_t spurious = 0xF0;

        constexpr uint64_t error_status = 0x280;

        constexpr uint64_t icr_low = 0x300;
        constexpr uint64_t icr_high = 0x310;
        constexpr uint64_t lvt_timer = 0x320;
        constexpr uint64_t lvt_pmc = 0x340;
        constexpr uint64_t lvt_lint0 = 0x350;
        constexpr uint64_t lvt_lint1 = 0x360;

        constexpr uint64_t timer_initial_count = 0x380;
        constexpr uint64_t timer_current_count = 0x390;
        constexpr uint64_t timer_divider = 0x3E0;

        enum class LapicTimerModes : uint8_t { OneShot = 0, Periodic, TscDeadline };
        enum class DestinationModes : uint8_t { Flat = 0xF, Cluster = 0 };
    } // namespace regs

    class Lapic {
        public:
        constexpr Lapic() : x2apic{false}, mmio_base{0}, ticks_per_ms{0} {};
        void init();
        void ipi(uint32_t id, uint8_t vector);
        void eoi();

        void start_timer(uint8_t vector, uint64_t ms, regs::LapicTimerModes mode);
        void install_pmc_irq(bool nmi, uint8_t vector = 0);
        bool pmc_irq_is_pending() const;

        private:
        uint64_t read(uint32_t reg) const;
        void write(uint32_t reg, uint64_t v);

        void calibrate_timer();

        bool x2apic;
        uintptr_t mmio_base;
        uint32_t ticks_per_ms;
    };
} // namespace apic
