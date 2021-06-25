#include <Luna/cpu/threads.hpp>
#include <Luna/cpu/cpu.hpp>

#include <Luna/misc/log.hpp>

#include <std/mutex.hpp>
#include <std/vector.hpp>

#include <Luna/cpu/idt.hpp>
#include <lai/helpers/pm.h>

struct ThreadIrqState {
    constexpr ThreadIrqState(): saved_if{false} {}
    void lock() {
        uint64_t rflags = 0;
        asm volatile("pushfq\r\npop %0" : "=r"(rflags));
        saved_if = (rflags >> 9) & 1;

        asm("cli");
    }

    void unlock() {
        if(saved_if)
            asm("sti");
        else
            asm("cli");
    }

    private:
    bool saved_if;
};

static IrqTicketLock scheduler_lock{};
static std::vector<threading::Thread*> threads;
static std::vector<uint8_t> waiting_cpus;
static size_t index = 0;

static threading::Thread* spawn_unlocked_ignored(void (*f)(void*), void* arg) {
    auto* thread = new threading::Thread();

    threading::init_thread_context(thread, f, arg);
    thread->state = threading::ThreadState::Ignore;
    threads.push_back(thread);

    return thread;
}

static threading::Thread* create_idle_thread() {
    auto* thread = spawn_unlocked_ignored([](void*) {
        // No work available, wait for an IPI that notifies us
        auto id = get_cpu().lapic_id;

        scheduler_lock.lock();
        waiting_cpus[id / 8] |= (1 << (id % 8));
        scheduler_lock.unlock();

        // There might be a race where after hlt takes an IRQ, one can happen between hlt and cli, i don't think this is
        // a problem, but let it be known
        asm("sti; hlt; cli");

        scheduler_lock.lock();
        waiting_cpus[id / 8] &= ~(1 << (id % 8));      
        scheduler_lock.unlock();
        
        kill_self();
    }, nullptr);

    thread->ctx.rflags &= ~(1 << 9); // Make sure the idle thread runs with IRQs disabled, to not allow IRQs from here, to then to occur, preventing scheduler deadlocks
    scheduler_lock.saved_if = false; // Make sure we stay CLI'ed until the context switch
    return thread;
}

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

    return nullptr;
}

threading::Thread* this_thread() {
    return get_cpu().current_thread;
}

#include <Luna/cpu/regs.hpp>

void yield() {
    ThreadIrqState thread_irq_state{};
    std::lock_guard guard{thread_irq_state}; // We need to keep rflags.IF seperate from the other thread state since we have other contraints on it

    scheduler_lock.lock();
    auto* old = this_thread();

    auto& current_thread = get_cpu().current_thread;
    current_thread = next_thread();
    if(!current_thread) { // No new thread? No biggie, we came from a thread, just return there
        scheduler_lock.unlock();
        return;
    }

    current_thread->state = threading::ThreadState::Running;
    scheduler_lock.unlock();

    threading::do_yield(&old->ctx, &old->state, &current_thread->ctx, (uint64_t)threading::ThreadState::Idle);
}

void await(threading::Event* event) {   
    ThreadIrqState thread_irq_state{};
    std::lock_guard guard{thread_irq_state}; // We need to keep rflags.IF seperate from the other thread state since we have other contraints on it

    scheduler_lock.lock();

    /*
        The lock above cli'ed, so we're safe, but if the event has already occured, next_thread won't find it -
        because we only get the Blocked status on the `do_yield` and the IRQ has already happened, so the idle thread-
        will "deadlock" the scheduler, this circumvents that. Since we're safe and cli'ed, any event that will happen before do_yield-
        has happened, and we just need to check for earlier events.
    */
    if(event->is_triggered()) {
        scheduler_lock.unlock();
        return;
    }

    auto* old = this_thread();
    old->current_event = event;

    auto& current_thread = get_cpu().current_thread;
    current_thread = next_thread();
    if(!current_thread)
        current_thread = create_idle_thread(); // We ofcourse cannot return to the previous thread, so we'll have to idle
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
    if(!current_thread)
        current_thread = create_idle_thread(); // We ofcourse cannot return to the previous thread, so we'll have to idle
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

    wakeup_cpu_unlocked();
}

threading::Thread* spawn(void (*f)(void*), void* arg) {
    auto* thread = new threading::Thread();

    threading::init_thread_context(thread, f, arg);
    threading::add_thread(thread);

    return thread;
}

void threading::start_on_cpu() {
    waiting_cpus.resize(get_cpu().lapic_id / 8 + 1); // TODO: This isn't very elegant, just get the number of cpus
    
    idt::set_handler(254, idt::handler{.f = [](uint8_t, idt::regs*, void*) {
        // Not much to do tbh
    }, .is_irq = true, .should_iret = true, .userptr = nullptr});

    auto& cpu = get_cpu();
    cpu.thread_kill_stack.init((size_t)0x1000);

    auto& current_thread = cpu.current_thread;

    scheduler_lock.lock();
    current_thread = next_thread();
    if(!current_thread)
        current_thread = create_idle_thread(); // There's no thread for us available, so we'll just have to wait
    ASSERT(current_thread);
    current_thread->state = threading::ThreadState::Running;
    scheduler_lock.unlock();
    
    thread_invoke(&current_thread->ctx);
}


void threading::wakeup_cpu_unlocked() {
    // Notify a waiting cpu that there's a new thread
    for(size_t i = 0; i < waiting_cpus.size(); i++) {
        if(waiting_cpus[i] != 0) {
            for(size_t j = 0; j < 8; j++) {
                if(waiting_cpus[i] & (1 << j)) {
                    get_cpu().lapic.ipi(8 * i + j, 254);
                    return;
                }
            }
        }
    }

}

void threading::wakeup_cpu() {
    std::lock_guard guard{scheduler_lock};
    wakeup_cpu_unlocked();
}