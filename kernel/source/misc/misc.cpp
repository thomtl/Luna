#include <Luna/misc/misc.hpp>

#include <Luna/misc/log.hpp>
#include <Luna/misc/debug.hpp>

void panic(const char* file, const char* func, size_t line, const char* msg){
    print("PANIC: {} {}:{} -> {}\n", file, func, line, msg);
    debug::trace_stack();

    asm("cli; hlt");

    while(1)
        ;
}