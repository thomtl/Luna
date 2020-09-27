#include <Luna/cpu/idt.hpp>
#include <Luna/cpu/gdt.hpp>

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
}

void idt::load() {
    table_pointer.set();
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
        print("Exception #{} ({}) has occurred\n", exception.mnemonic, exception.message);

        while(1)
            ;
    } else {
        print("IRQ{} has occurred\n", int_number);

        while(1)
            ;
    }
}