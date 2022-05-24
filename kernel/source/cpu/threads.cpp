#include <Luna/cpu/threads.hpp>
#include <Luna/cpu/idt.hpp>

#include <Luna/cpu/regs.hpp>

#include <Luna/drivers/timers/timers.hpp>

#include <Luna/misc/log.hpp>

#include <std/algorithm.hpp>
#include <std/vector.hpp>

static IrqTicketLock scheduler_lock{}; // TODO: Mostly replace global scheduler lock with per-thread lock
constinit static std::vector<threading::Thread*> threads;

using RunQueue = std::vector<threading::Thread*>; // TODO: Replace with std::deque
constinit static RunQueue queue1, queue2;
constinit static RunQueue* active_ptr = &queue1, *expired_ptr = &queue2;

static void idle();

static void rearm_preemption() {
    get_cpu().lapic.start_timer(threading::quantum_irq_vector, threading::quantum_time, lapic::regs::LapicTimerModes::OneShot);
}

static threading::Thread* next_thread() {
    if(active_ptr->size() == 0)
        std::swap(active_ptr, expired_ptr);

    if(active_ptr->size() == 0) // No threads in either queue
        return nullptr;

    auto* ret = active_ptr->back();
    active_ptr->pop_back();

    ASSERT(ret->state == threading::ThreadState::Idle);
    ASSERT(std::find(threads.begin(), threads.end(), ret) != threads.end());
    return ret;
}

threading::Thread* this_thread() {
    return get_cpu().current_thread;
}

void await(threading::Event* event) {   
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
    old->state = threading::ThreadState::Blocked;
    event->add_to_waiters(old);

    scheduler_lock.saved_if = false;
    scheduler_lock.unlock();

    asm("int $254\r\nsti"); // Yield
}

void kill_self() {
    scheduler_lock.lock();
    auto* self = this_thread();

    auto iter = threads.find(self);
    ASSERT(iter != threads.end());

    threads.erase(iter); // Bai bai
    ASSERT(std::find(active_ptr->begin(), active_ptr->end(), self) == active_ptr->end());
    ASSERT(std::find(expired_ptr->begin(), expired_ptr->end(), self) == expired_ptr->end());

    auto& current_thread = get_cpu().current_thread;
    current_thread = next_thread();

    bool should_idle = false;
    if(current_thread) { // If we were able to get a new thread
        current_thread->state = threading::ThreadState::Running;
    } else {
        should_idle = true; // If we weren't able to find a new thread we should idle
    }

    auto kill_stack = get_cpu().thread_kill_stack->top();

    scheduler_lock.unlock();

    // We need to run an epilogue on a different stack to avoid a UAF bug where the stack gets reused before we're done jumping to a new thread
    // And because stacks are cleared on initialization it jumps to NULL
    auto kill_epilogue = +[](threading::Thread* self, threading::Thread* next, bool should_idle) {
        delete self;

        rearm_preemption();
        if(should_idle)
            idle();
        else
            thread_invoke(&(next->ctx));

        __builtin_trap();
    };

    asm volatile(
            "mov %[new_stack], %%rsp\n"
            "xor %%rbp, %%rbp\n"
            "call *%[epilogue]" 
            : : "D"(self), "S"(current_thread), "d"(should_idle), [new_stack] "r"(kill_stack), [epilogue] "r"(kill_epilogue) : "memory");

    __builtin_trap();
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
    expired_ptr->push_back(thread);
}

void threading::wakeup_thread(Thread* thread) {
    std::lock_guard guard{scheduler_lock};

    ASSERT(thread->state == ThreadState::Blocked);
    thread->state = ThreadState::Idle;
    expired_ptr->push_back(thread);
}

threading::Thread* spawn(void (*f)(void*), void* arg) {
    auto* thread = new threading::Thread();

    threading::init_thread_context(thread, f, arg);
    threading::add_thread(thread);

    return thread;
}

static void idle() {
    auto idle_epilogue = +[] {
        // It is possible that another IRQ arrives that won't reschedule us
        // TODO: In that case should we force reschedule?
        while(1)
            asm("sti\r\nhlt");

        __builtin_trap();
    };

    asm volatile(
            "mov %[new_stack], %%rsp\n"
            "xor %%rbp, %%rbp\n"
            "call *%[func]" 
            : : [new_stack] "r"(get_cpu().thread_kill_stack->top()), [func] "r"(idle_epilogue) : "memory");

    __builtin_trap();
}

static void quantum_irq_handler(uint8_t, idt::regs* regs, void*) {
    scheduler_lock.lock();
    auto& cpu = get_cpu();
    auto* old_thread = cpu.current_thread;
    
    if(old_thread) { // old_thread might be null
        old_thread->ctx.save(regs);
        if(old_thread->state == threading::ThreadState::Running) {
            old_thread->state = threading::ThreadState::Idle;

            expired_ptr->push_back(old_thread);
        }
    }

    auto* next = next_thread();
    if(!next) {
        scheduler_lock.unlock();

        cpu.current_thread = nullptr;
        cpu.lapic.eoi();

        rearm_preemption();
        idle();

        __builtin_trap();
    }

    next->state = threading::ThreadState::Running;
    next->ctx.restore(regs);
    cpu.current_thread = next;

    rearm_preemption();
    scheduler_lock.unlock();
}

void threading::start_on_cpu() {
    auto& cpu = get_cpu();

    idt::set_handler(quantum_irq_vector, idt::handler{.f = quantum_irq_handler, .is_irq = true, .should_iret = true, .userptr = nullptr});
    
    cpu.thread_kill_stack.init((size_t)0x1000);
    cpu.current_thread = nullptr;

    asm("sti\r\nint $254"); // Enter timer handler
    __builtin_trap();
}


void threading::ThreadContext::save(const idt::regs* regs) {
    rax = regs->rax;
    rbx = regs->rbx;
    rcx = regs->rcx;
    rdx = regs->rdx;
    rsi = regs->rsi;
    rdi = regs->rdi;
    rbp = regs->rbp;

    r8 = regs->r8;
    r9 = regs->r9;
    r10 = regs->r10;
    r11 = regs->r11;
    r12 = regs->r12;
    r13 = regs->r13;
    r14 = regs->r14;
    r15 = regs->r15;

    rsp = regs->rsp;
    rip = regs->rip;
    rflags = regs->rflags;
}

void threading::ThreadContext::restore(idt::regs* regs) const {
    regs->rax = rax;
    regs->rbx = rbx;
    regs->rcx = rcx;
    regs->rdx = rdx;
    regs->rsi = rsi;
    regs->rdi = rdi;
    regs->rbp = rbp;

    regs->r8 = r8;
    regs->r9 = r9;
    regs->r10 = r10;
    regs->r11 = r11;
    regs->r12 = r12;
    regs->r13 = r13;
    regs->r14 = r14;
    regs->r15 = r15;

    regs->rsp = rsp;
    regs->rip = rip;
    regs->rflags = rflags;
}

void threading::Thread::pin_to_this_cpu() {
    std::lock_guard guard{scheduler_lock};
    cpu_pin = {.is_pinned = true, .cpu_id = get_cpu().lapic_id};
}

void threading::Thread::pin_to_cpu(uint32_t id)  {
    scheduler_lock.lock(); // Not using lock guard because deadlock on the int, TODO : proper yield
    cpu_pin = {.is_pinned = true, .cpu_id = id};
    scheduler_lock.unlock();

    asm("int $254"); // TODO: Proper yield to not fuck up next thread's quantum
}