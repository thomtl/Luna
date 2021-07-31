#pragma once

#include <Luna/cpu/cpu.hpp>
#include <std/mutex.hpp>

namespace std {
    template<typename T>
    struct EventQueue {
        void push(const T& v) {
            std::lock_guard guard{lock};

            queue.push_back(v);
            event.trigger();
        }

        template<typename F>
        void handle(F f) {
            std::lock_guard guard{lock};
            for(auto& v : queue)
                f(v);

            queue.clear();
        }

        template<typename F>
        void handle_await(F f) {
            ::await(&event);
            event.reset();

            handle(f);
        }

        //private:
        std::vector<T> queue;
        threading::Event event;
        IrqTicketLock lock;
    };
} // namespace std


