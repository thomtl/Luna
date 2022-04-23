#pragma once

#include <Luna/common.hpp>
#include <Luna/cpu/stack.hpp>

#include <std/vector.hpp>

namespace idt
{
    struct regs; // Forward decl
} // namespace idt


namespace threading {
    void start_on_cpu();

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

    // ACCESSED FROM ASSEMBLY, DO NOT CHANGE WITHOUT CHANGING THREADING.ASM
    struct [[gnu::packed]] ThreadContext {
        void save(const idt::regs* regs);
        void restore(idt::regs* regs) const;

        uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp, r8, r9, r10, r11, r12, r13, r14, r15;
        uint64_t rsp, rip, rflags;
    };

    enum class ThreadState : uint64_t { Idle = 0, Running = 1, Blocked = 2, Ignore = 3 };
    constexpr uint8_t quantum_irq_vector = 254;
    constexpr size_t quantum_time = 100; // ms

    struct Thread {
        Thread(): state(ThreadState::Idle), stack(0x4000), ctx(), cpu_pin({.is_pinned = false, .cpu_id = 0}), current_event(nullptr) {}
        
        ThreadState state;
        cpu::Stack stack;
        ThreadContext ctx;

        struct {
            bool is_pinned;
            uint32_t cpu_id;
        } cpu_pin;

        Event* current_event;

        void pin_to_this_cpu();
    };

    extern "C" void thread_invoke(ThreadContext* new_ctx);

    void init_thread_context(Thread* thread, void (*f)(void*), void* arg);
    void add_thread(Thread* thread);
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
        event.reset();

        ASSERT(constructed);
        return *reinterpret_cast<T*>(&object);
    }

    void set_value(const T& value) {
        new ((T*)object.data) T(value);
        constructed = true;

        event.trigger();
    }

    void reset() {
        event.reset();
    }

    private:
    std::aligned_storage_t<sizeof(T), alignof(T)> object;
    bool constructed = false;
    threading::Event event;
};

template<>
struct Promise<void> {
    void await() {
        ::await(&event);
        event.reset();
    }

    void complete() {
        event.trigger();
    }

    private:
    threading::Event event;
};
