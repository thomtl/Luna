#pragma once

#include <stdint.h>
#include <stddef.h>

[[noreturn]]
void panic(const char* file, const char* func, size_t line, const char* msg);

constexpr uintptr_t align_down(uintptr_t n, uintptr_t a) {
    return (n & ~(a - 1));
}

constexpr uintptr_t align_up(uintptr_t n, uintptr_t a) {
    return align_down(n + a - 1, a);
}