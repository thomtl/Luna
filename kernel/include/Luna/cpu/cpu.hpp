#pragma once

#include <Luna/common.hpp>
#include <Luna/cpu/regs.hpp>
#include <Luna/cpu/gdt.hpp>

struct CpuData {
    void* self;
    uint32_t lapic_id;
    gdt::table gdt_table;

    void set();
};

CpuData& get_cpu();

struct TicketLock {
    constexpr TicketLock(): serving{0}, next_ticket{0} {}

    void lock() {
        auto ticket = __atomic_fetch_add(&next_ticket, 1, __ATOMIC_SEQ_CST);
        while(!__atomic_compare_exchange(&serving, &ticket, &ticket, true, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
            asm("pause");
    }

    void unlock() {
        __atomic_add_fetch(&serving, 1, __ATOMIC_SEQ_CST);
    }

    private:
    uint32_t serving;
    uint32_t next_ticket;
};