#include <Luna/cpu/threads.hpp>
#include <Luna/cpu/idt.hpp>

#include <Luna/cpu/regs.hpp>

#include <Luna/cpu/tsc.hpp>

#include <Luna/misc/log.hpp>

#include <std/algorithm.hpp>
#include <std/vector.hpp>
#include <std/unordered_map.hpp>

static IrqTicketLock scheduler_lock{}; // TODO: Mostly replace global scheduler lock with per-thread lock
constinit static std::vector<threading::Thread*> threads;

using RunQueue = std::vector<threading::Thread*>; // TODO: Replace with std::deque
constinit static RunQueue queue1, queue2;
constinit static RunQueue* active_ptr = &queue1, *expired_ptr = &queue2;
constinit static std::lazy_initializer<std::unordered_map<uint32_t, RunQueue>> per_cpu_queue;

static void idle();

static void rearm_preemption() {
    get_cpu().lapic.start_timer(threading::quantum_irq_vector, threading::quantum_time, lapic::regs::LapicTimerModes::OneShot);
}

static threading::Thread* next_thread() {
    if(auto& queue = (*per_cpu_queue)[get_cpu().lapic_id]; !queue.empty()) {
        auto* ret = queue.back();
        queue.pop_back();
        return ret;
    }

    if(active_ptr->size() == 0)
        std::swap(active_ptr, expired_ptr);

    if(active_ptr->size() == 0) // No threads in either queue
        return nullptr;

    auto* ret = active_ptr->back();
    active_ptr->pop_back();

    ASSERT(ret->state == threading::ThreadState::Idle);
    ASSERT(std::find(threads.begin(), threads.end(), ret) != threads.end());
    if(ret->cpu_pin.is_pinned && ret->cpu_pin.cpu_id != get_cpu().lapic_id) {
        (*per_cpu_queue)[ret->cpu_pin.cpu_id].push_back(ret);
        return next_thread();
    }
    return ret;
}

threading::Thread* this_thread() {
    return get_cpu().current_thread;
}

void await(threading::Event* event) {
    auto* old = this_thread();
    old->lock.lock();
    event->lock.lock();

    if(event->is_triggered()) {
        event->lock.unlock();
        old->lock.unlock();

        return;
    }

    old->state = threading::ThreadState::Blocked;
    
    event->add_to_waiters(old);
    event->lock.unlock();

    old->lock.saved_if = false;
    old->lock.unlock();

    asm volatile("int %0\r\nsti" : : "i"(threading::quantum_irq_vector) : "memory"); // Yield
}

void kill_self() {
    auto* self = this_thread();
    self->lock.lock(); // Thread is getting killed so we don't have to unlock in the epilogue
    scheduler_lock.lock();

    auto iter = threads.find(self);
    ASSERT(iter != threads.end());

    threads.erase(iter); // Bai bai
    ASSERT(std::find(active_ptr->begin(), active_ptr->end(), self) == active_ptr->end());
    ASSERT(std::find(expired_ptr->begin(), expired_ptr->end(), self) == expired_ptr->end());

    get_cpu().current_thread = nullptr;

    scheduler_lock.saved_if = false;
    scheduler_lock.unlock();

    auto kill_stack = get_cpu().thread_kill_stack->top();

    // We need to run an epilogue on a different stack to avoid a UAF bug where the stack gets reused before we're done jumping to a new thread
    // And because stacks are cleared on initialization it jumps to NULL
    auto kill_epilogue = +[](threading::Thread* self) {
        delete self;

        asm volatile("int %0" : : "i"(threading::quantum_irq_vector) : "memory");
        __builtin_trap();
    };

    asm volatile(
            "mov %[new_stack], %%rsp\n"
            "xor %%rbp, %%rbp\n"
            "call *%[epilogue]" 
            : : "D"(self), [new_stack] "r"(kill_stack), [epilogue] "r"(kill_epilogue) : "memory");

    __builtin_trap();
}

void threading::init_thread_context(Thread* thread, void (*f)(void*), void* arg) {
    thread->ctx.rflags = (1 << 9) | (1 << 1);
    thread->ctx.rsp = (uint64_t)thread->stack.top();
    thread->ctx.rip = (uint64_t)f;
    thread->ctx.rdi = (uint64_t)arg;
}

void threading::add_thread(Thread* thread) {
    {
        std::lock_guard guard{thread->lock};
        threads.push_back(thread);
    }

    {
        std::lock_guard guard{scheduler_lock};
        expired_ptr->push_back(thread);
    }
}

void threading::wakeup_thread(Thread* thread) {
    {
        std::lock_guard guard{thread->lock};
        ASSERT(thread->state == ThreadState::Blocked);
        thread->state = ThreadState::Idle;
    }

    {
        std::lock_guard guard{scheduler_lock};
        active_ptr->push_back(thread); // Schedule ASAP
    }
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

static void quantum_irq_handler(uint8_t, idt::Regs* regs, void*) {
    auto entry_time = tsc::time_ns();

    auto& cpu = get_cpu();
    auto* old_thread = cpu.current_thread;
    
    if(old_thread) { // old_thread might be null
        std::lock_guard guard{old_thread->lock};

        old_thread->ctx.save(regs);
        old_thread->running_on_cpu = nullptr;
        old_thread->cpu_time += (entry_time - old_thread->cpu_time_at_scheduled_in);

        if(old_thread->state == threading::ThreadState::Running) {
            old_thread->state = threading::ThreadState::Idle;

            std::lock_guard guard{scheduler_lock};
            expired_ptr->push_back(old_thread);
        }
    }

    scheduler_lock.lock();
    auto* next = next_thread();
    scheduler_lock.unlock();
    if(!next) {
        cpu.current_thread = nullptr;
        cpu.lapic.eoi();

        rearm_preemption();
        idle();

        __builtin_trap();
    }

    std::lock_guard guard{next->lock};

    next->state = threading::ThreadState::Running;
    next->running_on_cpu = &cpu;
    next->ctx.restore(regs);
    cpu.current_thread = next;

    if(next->apc_queue.size() > 0) {
        next->apc_real_ret = regs->rip;

        regs->rip = (uint64_t)threading::thread_apc_trampoline;
        regs->rflags &= ~(1 << 9);
    }

    rearm_preemption();

    next->cpu_time_at_scheduled_in = tsc::time_ns();
}

void threading::start_on_cpu() {
    {
        std::lock_guard guard{scheduler_lock};
        per_cpu_queue.init();
    }

    auto& cpu = get_cpu();

    idt::set_handler(quantum_irq_vector, idt::Handler{.f = quantum_irq_handler, .is_irq = true, .should_iret = true, .userptr = nullptr});
    
    cpu.thread_kill_stack.init((size_t)0x1000);
    cpu.current_thread = nullptr;

    asm volatile("sti\r\nint %0" : : "i"(threading::quantum_irq_vector) : "memory"); // Enter timer handler
    __builtin_trap();
}


void threading::ThreadContext::save(const idt::Regs* regs) {
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

void threading::ThreadContext::restore(idt::Regs* regs) const {
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
    std::lock_guard guard{lock};
    cpu_pin = {.is_pinned = true, .cpu_id = get_cpu().lapic_id};
}

void threading::Thread::pin_to_cpu(uint32_t id)  {
    lock.lock();
    cpu_pin = {.is_pinned = true, .cpu_id = id};
    lock.unlock();

    asm volatile("int %0" : : "i"(threading::quantum_irq_vector) : "memory"); // Yield
}

uint64_t threading::Thread::time_ns() {
    return cpu_time + (tsc::time_ns() - cpu_time_at_scheduled_in);
}

uint64_t threading::Thread::time_ns_at(uint64_t count) {
    return cpu_time + (tsc::time_ns_at(count) - cpu_time_at_scheduled_in);
}


void threading::Thread::queue_apc(APCFunction func, void* userptr) {
    std::lock_guard guard{lock};

    ASSERT(apc_queue.size() <= 16); // Seems like a reasonable limit, something is probably wrong if we reach this

    apc_queue.emplace_back(func, userptr);
}

static void do_apcs(threading::Thread* thread) {
    for(auto& [f, userptr] : thread->apc_queue)
        f(userptr);

    thread->apc_queue.clear();
}

void threading::Thread::invoke_apcs() {
    std::lock_guard guard{lock};

    if(state != ThreadState::Running) {
        return; // Queued APCs will get executed when the thread is scheduled in again
    }

    // I don't know if Self-APCs are useful, but I guess we can support them here
    if(this == this_thread()) {
        do_apcs(this);

        return;
    }

    DEBUG_ASSERT(running_on_cpu);
    get_cpu().lapic.ipi(running_on_cpu->lapic_id, threading::quantum_irq_vector); // Will schedule the thread out, so from the perspective of the thread they run "immediately"
                                                                                  // TODO: More efficient APC dispatching, cannot do APC IPI because complex locking stuff    
}

extern "C" uint64_t thread_run_apcs() {
    auto* thread = this_thread();

    std::lock_guard guard{thread->lock};

    do_apcs(thread);

    return thread->apc_real_ret;
}