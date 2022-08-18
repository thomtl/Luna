#include <Luna/drivers/timers/timers.hpp>

#include <Luna/drivers/timers/hpet.hpp>

#include <std/linked_list.hpp>
#include <std/optional.hpp>
#include <std/mutex.hpp>

#include <Luna/misc/log.hpp>


static constinit std::intrusive::linked_list<timer::Timer> queue{};
static constinit IrqTicketLock lock{};
static constinit hpet::Comparator* timer_handle = nullptr;

static void arm_timer(uint64_t deadline);
static void dearm_timer();
static bool queue_event_(timer::Timer* event);

static void handle_events() {
    while(queue.size() > 0) {
        auto* event = &queue.front();

        if(event->current_deadline > hpet::time_ns())
            break; // Event hasn't occurred yet

        queue.pop_front();
        event->complete();
        if(event->is_periodic()) {
            event->current_deadline = hpet::time_ns() + event->period().ns();
            queue_event_(event);
        }
    }

    if(queue.size() > 0)
        arm_timer(queue.front().current_deadline);
}

static void timer_callback(void*) {
    std::lock_guard guard{lock};

    handle_events();   
}

static void arm_timer(uint64_t deadline) {
    auto curr_time = hpet::time_ns();
    if(curr_time >= deadline) {
        curr_time -= (1_ms).ns();
    }
    
    timer_handle->start_timer(false, TimePoint::from_ns(deadline - curr_time), timer_callback, nullptr);
}

static void dearm_timer() {
    timer_handle->cancel_timer();
}

static bool queue_event_(timer::Timer* event) {
    // This logic definitely depends on short-circuiting
    if((queue.size() == 0) || (queue.size() >= 1 && event->current_deadline < queue.front().current_deadline)) {
        queue.push_front(event);

        return true;
    } else {
        auto it = queue.begin();
        for(; it != queue.end(); ++it)
            if(event->current_deadline < it->current_deadline)
                break;

        queue.insert(it, event);

        return false;
    }
}

void timer::queue_event(timer::Timer* event) {
    std::lock_guard guard{lock};

    event->current_deadline = hpet::time_ns() + event->period().ns();

    if(queue_event_(event))
        arm_timer(queue.front().current_deadline);
}

void timer::dequeue_event(Timer* timer) {
    std::lock_guard guard{lock};

    dearm_timer();

    queue.erase(timer);

    if(queue.size() > 0)
        arm_timer(queue.front().current_deadline);
}

void timer::init() {
    timer_handle = hpet::allocate_comparator();
}