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

constexpr size_t div_ceil(size_t a, size_t b) {
    return (a + b - 1) / b;
}

constexpr uint32_t bswap32(uint32_t v) {
    return (uint32_t)((v >> 24) & 0xFF) | ((v << 8) & 0xFF0000) | ((v >> 8) & 0xFF00) | ((v << 24) & 0xFF000000);
}

constexpr uint16_t bswap16(uint16_t v) {
    return (v >> 8) | (v << 8);
}