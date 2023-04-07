#include <Luna/drivers/timers/timers.hpp>

#include <Luna/drivers/timers/hpet.hpp>

#include <std/linked_list.hpp>
#include <std/optional.hpp>
#include <std/mutex.hpp>

#include <Luna/misc/log.hpp>


static constinit std::intrusive::linked_list<timer::Timer> queue{};
static constinit IrqTicketLock lock{};
static constinit hpet::Comparator* timer_handle = nullptr;

static void queue_event_(timer::Timer* event);
static void timer_callback(void*);

static void handle_events() {
    if(queue.size() == 0)
        return;
    
    auto curr_time = hpet::time_ns();
    do {
        while(true) {
            if(queue.size() == 0) {
                timer_handle->cancel_timer();
                return;
            }

            auto* event = &queue.front();
            if(event->current_deadline > curr_time)
                break; // Event hasn't occurred yet

            queue.pop_front();
            
            event->complete();
            if(event->is_periodic()) {
                event->current_deadline = curr_time + event->period().ns();
                queue_event_(event);
            }
        }

        DEBUG_ASSERT(queue.size() > 0);
        timer_handle->start_timer(false, TimePoint::from_ns(queue.front().current_deadline - curr_time), timer_callback, nullptr);
        curr_time = hpet::time_ns();
    } while(queue.front().current_deadline <= curr_time);
}

static void timer_callback(void*) {
    std::lock_guard guard{lock};

    handle_events();   
}

static void queue_event_(timer::Timer* event) {
    // This logic definitely depends on short-circuiting
    if((queue.size() == 0) || (queue.size() >= 1 && event->current_deadline < queue.front().current_deadline)) {
        queue.push_front(event);
    } else {
        auto it = queue.begin();
        for(; it != queue.end(); ++it)
            if(event->current_deadline < it->current_deadline)
                break;

        queue.insert(it, event);
    }
}

void timer::queue_event(timer::Timer* event) {
    std::lock_guard guard{lock};

    event->current_deadline = hpet::time_ns() + event->period().ns();
    queue_event_(event);

    handle_events();
}

void timer::dequeue_event(Timer* timer) {
    std::lock_guard guard{lock};

    queue.erase(timer);
    handle_events();
}

void timer::init() {
    timer_handle = hpet::allocate_comparator();
}