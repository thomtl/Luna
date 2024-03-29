#pragma once

#include <Luna/common.hpp>
#include <Luna/misc/time.hpp>

#include <std/linked_list.hpp>
#include <std/mutex.hpp>

namespace timer {
    struct Timer;
    
    void queue_event(Timer* timer);
    void dequeue_event(Timer* timer);
    void init();

    struct Timer : public std::intrusive::list_node<Timer> {
        Timer(): _period{}, _periodic{false}, _is_queued{false}, _f{nullptr}, _userptr{nullptr} {}
        Timer(const TimePoint& period, bool periodic, void (*f)(void*), void* userptr = nullptr): _period{period}, _periodic{periodic}, _is_queued{false}, _f{f}, _userptr{userptr} {}
        ~Timer() {
            if(_is_queued)
                stop();
        }

        Timer(Timer&&) = delete;
        Timer(const Timer&) = delete;
        Timer& operator=(Timer&&) = delete;
        Timer& operator=(const Timer&) = delete;

        void set_handler(void (*f)(void*), void* userptr) {
            std::lock_guard guard{_lock};
            
            _f = f;
            _userptr = userptr;
        }

        void setup(const TimePoint& period, bool periodic) {
            dequeue_event(this);

            {
                std::lock_guard guard{_lock};

                _period = period;
                _periodic = periodic;
            }
            
            queue_event(this);
        }

        void start() {
            queue_event(this);
        }
        
        void stop() {
            dequeue_event(this);
        }

        void complete() {
            std::lock_guard guard{_lock};

            DEBUG_ASSERT(_is_queued);

            _f(_userptr);

            _is_queued = _periodic; // If we're periodic we'll be requeued
        }

        uint64_t current_deadline;

        TimePoint period() const { return _period; }
        bool is_periodic() const { return _periodic; }
        

        private:
        friend void queue_event(Timer* timer);
        friend void dequeue_event(Timer* timer);

        IrqTicketLock _lock;

        TimePoint _period;
        bool _periodic;
        bool _is_queued;

        void (*_f)(void*);
        void* _userptr;
    };
} // namespace timers