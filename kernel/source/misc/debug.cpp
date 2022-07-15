#include <Luna/misc/debug.hpp>
#include <Luna/misc/log.hpp>
#include <Luna/cpu/idt.hpp>

void debug::trace_stack(uintptr_t rbp) {
    struct Frame {
        Frame* rbp;
        uint64_t rip;
    };

    auto* current = (Frame*)rbp;

	print("Stack Trace:\n");
    size_t i = 0;
	while(true) {
		if(!current)
    		break;

        if(current->rip == 0)
            break;
		
        print("  {}: RIP: {:#x}\n", i++, current->rip);		
		
        current = current->rbp;
	}
}

void debug::trace_stack() {
    trace_stack((uintptr_t)__builtin_frame_address(0));
}

void debug::poison_addr(uint8_t n, uintptr_t addr, uint8_t flags) {
    if(n == 0) asm volatile("mov %0, %%dr0" : : "r"(addr) : "memory");
    else if(n == 1) asm volatile("mov %0, %%dr1" : : "r"(addr) : "memory");
    else if(n == 2) asm volatile("mov %0, %%dr2" : : "r"(addr) : "memory");
    else if(n == 3) asm volatile("mov %0, %%dr3" : : "r"(addr) : "memory");

    uint64_t dr7 = 0;
    asm volatile("mov %%dr7, %0" : "=r"(dr7) : : "memory");

    dr7 |= (1 << (2 * (n + 1) - 1));

    dr7 &= ~(0xF << (4 * n + 16));
    dr7 |= (flags << (4 * n + 16));

    asm volatile("mov %0, %%dr7" : : "r"(dr7) : "memory");

    idt::set_handler(1, idt::Handler{.f = [](uint8_t, idt::Regs* regs, void*) {
        uint64_t dr6 = 0;
        asm volatile("mov %%dr6, %0" : "=r"(dr6) : : "memory");

        for(size_t i = 0; i < 3; i++) {
            if(!(dr6 & (1 << i)))
                continue;

            print("debug: Poisoned access {}\n", i);

            uintptr_t addr = 0;
            if(i == 0) asm volatile("mov %%dr0, %0" : "=r"(addr) : : "memory");
            else if(i == 1) asm volatile("mov %%dr1, %0" : "=r"(addr) : : "memory");
            else if(i == 2) asm volatile("mov %%dr2, %0" : "=r"(addr) : : "memory");
            else if(i == 3) asm volatile("mov %%dr3, %0" : "=r"(addr) : : "memory");

            auto rip = regs->rip, rbp = regs->rbp;
            print("       Addr: {:#x}\n", addr);
            print("       RIP: {:#x}\n", rip);

            debug::trace_stack(rbp);
        }

        dr6 &= ~0xF;
        asm volatile("mov %0, %%dr6" : : "r"(dr6) : "memory");
    }, .is_irq = false, .should_iret = true, .userptr = nullptr});
}