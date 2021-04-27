#pragma once

#include <Luna/common.hpp>
#include <Luna/cpu/stack.hpp>

namespace threading {
    struct Event {
        constexpr Event(): value(0) {}

        void trigger() {
            __atomic_store_n(&value, 1, __ATOMIC_SEQ_CST);
        }

        void reset() {
            __atomic_store_n(&value, 0, __ATOMIC_SEQ_CST);
        }

        bool is_triggered() {
            return __atomic_load_n(&value, __ATOMIC_SEQ_CST) == 1;
        }

        private:
        uint64_t value;
    };

    // THIS STRUCT IS ACCESSED FROM ASSEMBLY, DO NOT CHANGE
    struct ThreadContext {
        uint64_t rbx, rbp, rsp, r12, r13, r14, r15, rdi;
        uint64_t rip, rflags;
    };

    enum class ThreadState : uint64_t { Idle = 0, Running = 1, Blocked = 2 };

    struct Thread {
        Thread(): state(ThreadState::Idle), stack(0x4000), ctx(), current_event(nullptr) {}
        
        ThreadState state;
        cpu::Stack stack;
        ThreadContext ctx;

        Event* current_event;
    };

    extern "C" void thread_invoke(ThreadContext* new_ctx);
    extern "C" void do_yield(ThreadContext* old_ctx, ThreadState* state, ThreadContext* new_ctx, uint64_t new_state);

    void start_on_cpu();

    void init_thread_context(Thread* thread, void (*f)(void*), void* arg);
    void add_thread(Thread* thread);
} // namespace threading

threading::Thread* this_thread();

void yield();
void await(threading::Event* event);
void kill_self();

threading::Thread* spawn(void (*f)(void*), void* arg);

template<typename F>
threading::Thread* spawn(F f) {
    auto trampoline = [](void* arg) {
        auto* func = (F*)arg;
        ASSERT(func);
        (*func)();

        PANIC("Returned from thread trampoline");
    };

    auto* thread = new threading::Thread();
    
    auto* item = thread->stack.push<F>(f);
    threading::init_thread_context(thread, trampoline, item);
    threading::add_thread(thread);

    return thread;
}

template<typename T>
struct Promise {
    T& await() {
        ::await(&event);
        event.reset();

        return (T&)(*(T*)object.data);
    }

    void set_value(const T& value) {
        new ((T*)object.data) T(value);

        event.trigger();
    }

    private:
    std::aligned_storage_t<sizeof(T), alignof(T)> object;
    threading::Event event;
};