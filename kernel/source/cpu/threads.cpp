#include <Luna/cpu/threads.hpp>
#include <Luna/cpu/cpu.hpp>

#include <Luna/misc/log.hpp>

#include <std/mutex.hpp>
#include <std/vector.hpp>

#include <Luna/cpu/idt.hpp>
#include <lai/helpers/pm.h>

static TicketLock scheduler_lock{};
static std::vector<threading::Thread*> threads;
static size_t index = 0;

static threading::Thread* spawn_unlocked(void (*f)(void*), void* arg) {
    auto* thread = new threading::Thread();

    threading::init_thread_context(thread, f, arg);
    threads.push_back(thread);

    return thread;
}

#include <Luna/drivers/hpet.hpp>

static threading::Thread* next_thread() {
    for(size_t i = 0; i < threads.size(); i++) {
        index = (index + 1) % threads.size();
        
        if(threads[index]->state == threading::ThreadState::Idle) {
            return threads[index];
        } else if(threads[index]->state == threading::ThreadState::Blocked) {
            ASSERT(threads[index]->current_event);
            if(threads[index]->current_event->is_triggered())
                return threads[index];
        }
    }

    // Create a No-op thread, should eventually balance out, where there are enough No-op threads out there to balance all CPUs
    return spawn_unlocked([](void*) { while(1) { yield(); } }, nullptr);
}

threading::Thread* this_thread() {
    return get_cpu().current_thread;
}

#include <Luna/cpu/regs.hpp>

void yield() {
    scheduler_lock.lock();
    auto* old = this_thread();

    auto& current_thread = get_cpu().current_thread;
    current_thread = next_thread();
    current_thread->state = threading::ThreadState::Running;
    scheduler_lock.unlock();

    threading::do_yield(&old->ctx, &old->state, &current_thread->ctx, (uint64_t)threading::ThreadState::Idle);
}

void await(threading::Event* event) {
    scheduler_lock.lock();
    auto* old = this_thread();
    old->current_event = event;

    auto& current_thread = get_cpu().current_thread;
    current_thread = next_thread();
    current_thread->state = threading::ThreadState::Running;
    scheduler_lock.unlock();

    threading::do_yield(&old->ctx, &old->state, &current_thread->ctx, (uint64_t)threading::ThreadState::Blocked);
}

void kill_self() {
    scheduler_lock.lock();
    auto* self = this_thread();

    auto iter = threads.find(self);
    ASSERT(iter != threads.end());

    threads.erase(iter); // Bai bai

    auto& current_thread = get_cpu().current_thread;
    current_thread = next_thread();
    current_thread->state = threading::ThreadState::Running;
    scheduler_lock.unlock();

    // We need to run an epilogue on a different stack to avoid a UAF bug where the stack gets reused before we're done jumping to a new thread
    // And because stacks are cleared on initialization it jumps to NULL
    auto kill_epilogue = +[](threading::Thread* self, threading::Thread* next) {
        delete self;
        thread_invoke(&next->ctx);

        __builtin_unreachable();
    };

    asm volatile(
            "mov %[new_stack], %%rsp\n"
            "xor %%rbp, %%rbp\n"
            "call *%[epilogue]" 
            : : "D"(self), "S"(current_thread), [new_stack] "r"(get_cpu().thread_kill_stack->top()), [epilogue] "r"(kill_epilogue) : "memory");

    __builtin_unreachable();
}

void threading::init_thread_context(Thread* thread, void (*f)(void*), void* arg) {
    thread->ctx.rflags = (1 << 9) | (1 << 1);
    thread->ctx.rsp = (uint64_t)thread->stack.top();
    thread->ctx.rip = (uint64_t)f;
    thread->ctx.rdi = (uint64_t)arg;
}

void threading::add_thread(Thread* thread) {
    std::lock_guard guard{scheduler_lock};

    threads.push_back(thread);
}

threading::Thread* spawn(void (*f)(void*), void* arg) {
    auto* thread = new threading::Thread();

    threading::init_thread_context(thread, f, arg);
    threading::add_thread(thread);

    return thread;
}

void threading::start_on_cpu() {
    auto& cpu = get_cpu();
    cpu.thread_kill_stack.init((size_t)0x1000);

    auto& current_thread = cpu.current_thread;

    scheduler_lock.lock();
    current_thread = next_thread();
    ASSERT(current_thread);
    current_thread->state = threading::ThreadState::Running;
    scheduler_lock.unlock();
    
    thread_invoke(&current_thread->ctx);
}