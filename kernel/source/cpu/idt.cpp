#include <Luna/cpu/idt.hpp>
#include <Luna/cpu/gdt.hpp>

#include <Luna/cpu/regs.hpp>

static idt::handler handlers[idt::n_table_entries] = {};
static idt::entry table[idt::n_table_entries] = {};
static idt::pointer table_pointer{};

extern "C" void* isr_array_begin;
extern "C" void* isr_array_end;

static auto isr_array_begin_addr = (uintptr_t)&isr_array_begin;
static auto isr_array_end_addr = (uintptr_t)&isr_array_end;

void idt::init_table() {
    auto* isrs = (uintptr_t*)isr_array_begin_addr;
    size_t n_irqs = (isr_array_end_addr - isr_array_begin_addr) / sizeof(void*);

    for(size_t i = 0; i < n_irqs; i++)
        table[i] = entry{isrs[i], gdt::kcode};

    table_pointer.table = (uint64_t)&table;
    table_pointer.size = (sizeof(entry) * idt::n_table_entries) - 1;

    for(size_t i = 0; i < 32; i++)
        handlers[i].is_reserved = true; // Reserve Exceptions
}

void idt::load() {
    table_pointer.load();
}

void idt::set_handler(uint8_t vector, const handler& h) {
    handlers[vector] = h;
    handlers[vector].is_reserved = true;
}

uint8_t idt::allocate_vector() {
    // Skip IRQ255, since thats used for Spurious IRQs
    for(size_t i = idt::n_table_entries - 2; i > 0u; i--) {
        if(!handlers[i].is_reserved) {
            handlers[i].is_reserved = true;
            return i;
        }
    }

    PANIC("Couldn't allocate IDT vector");
}

void idt::reserve_vector(uint8_t vector) {
    handlers[vector].is_reserved = true;
}

struct {
    const char* mnemonic;
    const char* message;
} exceptions[] = {
    {.mnemonic = "DE", .message = "Division By Zero"},
    {.mnemonic = "DB", .message = "Debug"},
    {.mnemonic = "NMI", .message = "Non Maskable Interrupt"},
    {.mnemonic = "BP", .message = "Breakpoint"},
    {.mnemonic = "OF", .message = "Overflow"},
    {.mnemonic = "BR", .message = "Out of Bounds"},
    {.mnemonic = "UD", .message = "Invalid Opcode"},
    {.mnemonic = "NM", .message = "No Coprocessor"},
    {.mnemonic = "DF", .message = "Double Fault"},
    {.mnemonic = "-", .message = "Coprocessor Segment Overrun"},
    {.mnemonic = "TS", .message = "Invalid TSS"},
    {.mnemonic = "NP", .message = "Segment Not Present"},
    {.mnemonic = "SS", .message = "Stack Fault"},
    {.mnemonic = "GP", .message = "General Protection Fault"},
    {.mnemonic = "PF", .message = "Page Fault"},
    {.mnemonic = "-", .message = "Reserved"},
    {.mnemonic = "MF", .message = "x87 Floating-Point Exception"},
    {.mnemonic = "AC", .message = "Alignment Check"},
    {.mnemonic = "MC", .message = "Machine Check"},
    {.mnemonic = "XM/XF", .message = "SIMD Floating-Point Exception"},
    {.mnemonic = "VE", .message = "Virtualization Exception"},
    {.mnemonic = "CP", .message = "Control Protection Exception"},
    {.mnemonic = "-", .message = "Reserved"},
    {.mnemonic = "-", .message = "Reserved"},
    {.mnemonic = "-", .message = "Reserved"},
    {.mnemonic = "-", .message = "Reserved"},
    {.mnemonic = "-", .message = "Reserved"},
    {.mnemonic = "-", .message = "Reserved"},
    {.mnemonic = "-", .message = "Reserved"},
    {.mnemonic = "-", .message = "Reserved"},
    {.mnemonic = "SX", .message = "Security Exception"},
    {.mnemonic = "-", .message = "Reserved"},
};

extern "C" void isr_handler(idt::regs* regs) {
    auto int_number = regs->int_num & 0xFF;

    if(int_number < 32) {
        auto& exception = exceptions[int_number];
        print("Unhandled Exception #{} ({}) has occurred\n", exception.mnemonic, exception.message);

        uint64_t cr2;
        asm("mov %%cr2, %0" : "=r"(cr2));

        uint64_t cr3;
        asm("mov %%cr3, %0" : "=r"(cr3));

        const auto r = *regs;
        
        print("ERR: {:#x}, RIP: {:#x}, RFLAGS: {:#x}\n", (uint64_t)r.error_code, r.rip, r.rflags);
        print("RAX: {:#x}, RBX: {:#x}, RCX: {:#x}, RDX: {:#x}\n", r.rax, r.rbx, r.rcx, r.rdx);
        print("RSI: {:#x}, RDI: {:#x}, RBP: {:#x}, RSP: {:#x}\n", r.rsi, r.rdi, r.rbp, r.rsp);
        print("R8: {:#x}, R9: {:#x}, R10: {:#x}, R11: {:#x}\n", r.r8, r.r9, r.r10, r.r11);
        print("R12: {:#x}, R13: {:#x}, R14: {:#x}, R15: {:#x}\n", r.r12, r.r13, r.r14, r.r15);
        print("CS: {:#x}, SS: {:#x}, DS: {:#x}\n", r.cs, r.ss, r.ds);
        print("CR2: {:#x}, CR3: {:#x}\n", cr2, cr3);

        print("GS: {:#x}\n", msr::read(msr::gs_base));

        print("CPU: {:#x}\n", get_cpu().lapic_id);

        while(1)
            ;
    }

    if(handlers[int_number].f)
        handlers[int_number].f(regs, handlers[int_number].userptr);

    if(handlers[int_number].is_irq)
        get_cpu().lapic.eoi();

    if(!handlers[int_number].should_iret && !handlers[int_number].is_irq)
        asm("cli; hlt");
}