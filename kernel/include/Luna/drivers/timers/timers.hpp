#pragma once

#include <Luna/common.hpp>

namespace timers {
    struct HardwareTimerCapabilities {
        size_t period;
        bool can_be_periodic, has_irq, has_poll_ms, has_poll_ns, has_time_ns;
    };

    class AbstractHardwareTimer {
        public:
        virtual ~AbstractHardwareTimer() { }

        virtual HardwareTimerCapabilities get_capabilities() = 0;
        virtual bool start_timer([[maybe_unused]] bool periodic, [[maybe_unused]] uint64_t ms, [[maybe_unused]] void(*f)(void*), [[maybe_unused]] void* userptr = nullptr) { PANIC("Not supported"); }
        
        virtual void poll_msleep([[maybe_unused]] size_t ms) { PANIC("Not supported"); }
        virtual void poll_nsleep([[maybe_unused]] size_t ns) { PANIC("Not supported"); }
        virtual uint64_t time_ns() { PANIC("Not supported"); }
    };

    void register_timer(AbstractHardwareTimer* timer);

    void poll_msleep(uint64_t ms);
    void poll_nsleep(uint64_t ns);
    uint64_t time_ns();

    bool start_timer(bool periodic, uint64_t ms, void(*f)(void*), void* userptr = nullptr);
} // namespace timers
