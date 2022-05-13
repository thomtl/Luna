#include <Luna/drivers/timers/timers.hpp>
#include <Luna/cpu/cpu.hpp>

#include <std/mutex.hpp>
#include <std/linked_list.hpp>


static std::linked_list<timers::AbstractHardwareTimer*> timer_list;
static IrqTicketLock lock{};

static timers::AbstractHardwareTimer* poll_msleep_impl = nullptr;
static timers::AbstractHardwareTimer* poll_nsleep_impl = nullptr;
static timers::AbstractHardwareTimer* time_ns_impl = nullptr;

void timers::register_timer(timers::AbstractHardwareTimer* timer) {
    std::lock_guard guard{lock};

    timer_list.emplace_back(timer);

    auto cap = timer->get_capabilities();

    // TODO: Prefer certain timers, i.e. rdtsc > hpet
    if(cap.has_poll_ms)
        poll_msleep_impl = timer;

    if(cap.has_poll_ns)
        poll_nsleep_impl = timer;

    if(cap.has_time_ns)
        time_ns_impl = timer;
}

void timers::poll_msleep(uint64_t ms) {
    ASSERT(poll_msleep_impl);

    poll_msleep_impl->poll_msleep(ms);
}

void timers::poll_nsleep(uint64_t ns) {
    ASSERT(poll_nsleep_impl);

    poll_nsleep_impl->poll_nsleep(ns);
}

uint64_t timers::time_ns() {
    ASSERT(time_ns_impl);

    return time_ns_impl->time_ns();
}

bool timers::start_timer_ms(bool periodic, uint64_t ms, void(*f)(void*), void* userptr) {
    std::lock_guard guard{lock};
    for(auto timer : timer_list) {
        auto cap = timer->get_capabilities();
        if(!cap.has_irq)
            continue;
            
        if(periodic && !cap.can_be_periodic)
            continue;

        if(timer->start_timer(periodic, ms * (uint64_t)1e6, f, userptr))
            return true;        
    }

    return false;
}

bool timers::start_timer_ns(bool periodic, uint64_t ns, void(*f)(void*), void* userptr) {
    std::lock_guard guard{lock};
    for(auto timer : timer_list) {
        auto cap = timer->get_capabilities();
        if(!cap.has_irq)
            continue;
            
        if(periodic && !cap.can_be_periodic)
            continue;

        if(timer->start_timer(periodic, ns, f, userptr))
            return true;        
    }

    return false;
}