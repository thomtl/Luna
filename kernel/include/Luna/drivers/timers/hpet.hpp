#pragma once

#include <Luna/common.hpp>
#include <Luna/drivers/acpi.hpp>
#include <Luna/drivers/timers/timers.hpp>

namespace hpet {
    struct [[gnu::packed]] Regs {
        uint64_t cap;
        uint64_t reserved;
        uint64_t cmd;
        uint64_t reserved_1;
        uint64_t irq_status;
        uint8_t reserved_2[0xC8];
        uint64_t main_counter;
        uint64_t reserved_3;
        struct [[gnu::packed]] {
            uint64_t cmd;
            uint64_t value;
            uint64_t fsb;
            uint64_t reserved;
        } comparators[32];
    };

    struct Device;

    struct Comparator : public timers::AbstractHardwareTimer {
        timers::HardwareTimerCapabilities get_capabilities() override;
        bool start_timer(bool periodic, uint64_t ns, void(*f)(void*), void* userptr) override;
        void poll_msleep(size_t ms) override;
        void poll_nsleep(size_t ns) override;

        uint64_t time_ns() override;

        private:
        Device* _device;
        uint8_t _i;

        bool supports_fsb, supports_periodic;
        uint32_t ioapic_route;

        uint8_t vector;
        bool is_periodic;
        void(*f)(void*);
        void* userptr;

        friend struct Device;
    };

    struct Device {
        Device(acpi::Hpet* table);

        void poll_msleep(uint64_t ms);
        void poll_nsleep(uint64_t ns);
        uint64_t time_ns();

        private:
        void stop_timer() { regs->cmd &= ~1; }
        void start_timer() { regs->cmd |= 1; }

        acpi::Hpet* table;

        volatile Regs* regs;
        
        uint8_t n_comparators;
        uint64_t period;
        
        Comparator comparators[32];

        friend struct Comparator;
    };

    void init();
} // namespace hpet
