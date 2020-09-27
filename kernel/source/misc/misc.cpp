#include <Luna/misc/misc.hpp>

#include <Luna/misc/format.hpp>

void panic(const char* file, const char* func, size_t line, const char* msg){
    print("PANIC: {} {}:{} -> {}\n", file, func, line, msg);

    asm("cli; hlt");

    while(1)
        ;
}