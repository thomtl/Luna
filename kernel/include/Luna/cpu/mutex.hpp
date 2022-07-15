#pragma once

// This file is included from <Luna/common.hpp> so do stdint.h
#include <stdint.h>

struct TicketLock {
    constexpr TicketLock(): serving{0}, next_ticket{0} {}

    void lock() {
        auto ticket = __atomic_fetch_add(&next_ticket, 1, __ATOMIC_SEQ_CST);
        while(__atomic_load_n(&serving, __ATOMIC_SEQ_CST) != ticket)
            asm("pause");
    }

    void unlock() {
        __atomic_add_fetch(&serving, 1, __ATOMIC_SEQ_CST);
    }

    private:
    volatile uint64_t serving;
    volatile uint64_t next_ticket;
};

struct IrqTicketLock {
    constexpr IrqTicketLock(): saved_if{false}, _lock{} {}

    void lock() {
        uint64_t rflags = 0;
        asm volatile("pushfq\r\npop %0" : "=r"(rflags));
        bool tmp_if = (rflags >> 9) & 1;

        asm volatile("cli");
        _lock.lock();

        saved_if = tmp_if;
    }

    void unlock() {
        bool tmp_if = saved_if;
        _lock.unlock();

        if(tmp_if)
            asm volatile("sti");
        else
            asm volatile("cli");
    }

    bool saved_if;
    private:
    TicketLock _lock;
};