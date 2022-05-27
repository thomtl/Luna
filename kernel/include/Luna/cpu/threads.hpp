#pragma once

#include <Luna/common.hpp>
#include <Luna/cpu/cpu.hpp>
#include <Luna/cpu/stack.hpp>
#include <Luna/misc/log.hpp>
#include <std/mutex.hpp>
#include <std/vector.hpp>

namespace idt
{
    struct regs; // Forward decl
} // namespace idt


namespace threading {
    struct Thread; 
    struct ThreadContext;

    void start_on_cpu();

    extern "C" void thread_invoke(ThreadContext* new_ctx);

    void init_thread_context(Thread* thread, void (*f)(void*), void* arg);
    void add_thread(Thread* thread);
    void wakeup_thread(Thread* thread);

    // ACCESSED FROM ASSEMBLY, DO NOT CHANGE WITHOUT CHANGING THREADING.ASM
    struct [[gnu::packed]] ThreadContext {
        void save(const idt::regs* regs);
        void restore(idt::regs* regs) const;

        uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp, r8, r9, r10, r11, r12, r13, r14, r15;
        uint64_t rsp, rip, rflags;
    };

    enum class ThreadState : uint64_t { Idle = 0, Running = 1, Blocked = 2, Ignore = 3 };
    constexpr uint8_t quantum_irq_vector = 254; // Update hardcoded vector in start_on_cpu()
    constexpr size_t quantum_time = 10; // ms

    struct Thread {
        Thread(): state(ThreadState::Idle), stack(0x4000), ctx(), cpu_pin({.is_pinned = false, .cpu_id = 0}) {}

        IrqTicketLock lock;
        
        ThreadState state;
        cpu::Stack stack;
        ThreadContext ctx;

        struct {
            bool is_pinned;
            uint32_t cpu_id;
        } cpu_pin;

        void pin_to_this_cpu();
        void pin_to_cpu(uint32_t id);
    };

    struct Event {
        constexpr Event(): value(0), waiter(nullptr) {}

        void trigger() {
            std::lock_guard guard{lock};
            __atomic_store_n(&value, 1, __ATOMIC_SEQ_CST);

            if(waiter) {
                wakeup_thread(waiter);
                waiter = nullptr;
            }       
        }

        void reset() {
            __atomic_store_n(&value, 0, __ATOMIC_SEQ_CST);
        }

        bool is_triggered() const {
            return __atomic_load_n(&value, __ATOMIC_SEQ_CST) == 1;
        }

        void add_to_waiters(Thread* thread) {
            std::lock_guard guard{lock};

            ASSERT(!waiter);
            waiter = thread;
        }

        private:
        IrqTicketLock lock;
        uint64_t value;

        Thread* waiter;
    };
} // namespace threading

threading::Thread* this_thread();

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

        return get_value();
    }

    void set_value(const T& value) {
        new ((T*)object.data) T(value);

        event.trigger();
    }

    bool is_done() const {
        return event.is_triggered();
    }

    T& get_value() {
        ASSERT(is_done());
        return *reinterpret_cast<T*>(object.data);
    }

    private:
    std::aligned_storage_t<sizeof(T), alignof(T)> object;
    threading::Event event;
};

template<>
struct Promise<void> {
    void await() {
        ::await(&event);
    }

    void complete() {
        event.trigger();
    }

    bool is_done() const {
        return event.is_triggered();
    }

    private:
    threading::Event event;
};
