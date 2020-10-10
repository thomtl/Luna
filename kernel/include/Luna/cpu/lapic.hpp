#pragma once

#include <Luna/common.hpp>

namespace lapic
{
    namespace regs
    {
        constexpr uint32_t id = 0x20;
        constexpr uint32_t version = 0x30;
        constexpr uint32_t tpr = 0x80;
        constexpr uint32_t apr = 0x90;
        constexpr uint32_t ppr = 0xA0;
        constexpr uint32_t eoi = 0xB0;
        constexpr uint32_t rrd = 0xC0;
        constexpr uint32_t ldr = 0xD0;
        constexpr uint32_t dfr = 0xE0;
        constexpr uint32_t spurious = 0xF0;

        constexpr uint32_t lvt_timer = 0x320;

        constexpr uint32_t timer_initial_count = 0x380;
        constexpr uint32_t timer_current_count = 0x390;
        constexpr uint32_t timer_divider = 0x380;

        enum class LapicTimerModes : uint8_t { OneShot, Periodic, TscDeadline };
    } // namespace regs

    class Lapic {
        public:
        Lapic() : x2apic{false}, mmio_base{0}, ticks_per_ms{0} {};
        void init();
        void eoi();

        void start_timer(uint8_t vector, uint64_t ms, regs::LapicTimerModes mode, void (*poll)(uint64_t ms));

        private:
        uint32_t read(uint32_t reg);
        void write(uint32_t reg, uint32_t v);

        bool x2apic;
        uintptr_t mmio_base;
        uint32_t ticks_per_ms;
    };
} // namespace apic
