#pragma once

#include <stdint.h>
#include <stddef.h>

#include <std/type_traits.hpp>

[[noreturn]]
void panic(const char* file, const char* func, size_t line, const char* msg);

constexpr uintptr_t align_down(uintptr_t n, uintptr_t a) {
    return (n & ~(a - 1));
}

constexpr uintptr_t align_up(uintptr_t n, uintptr_t a) {
    return align_down(n + a - 1, a);
}

template<typename T>
constexpr T min(T a, std::type_identity_t<T> b) {
    return (a < b) ? a : b;
}

template<typename T>
constexpr T max(T a, std::type_identity_t<T> b) {
    return (a > b) ? a : b;
}

template<typename T>
constexpr T clamp(T v, std::type_identity_t<T> minimum, std::type_identity_t<T> maximum) {
    return min(max(v, minimum), maximum);
}

constexpr size_t div_ceil(size_t a, size_t b) {
    return (a + b - 1) / b;
}

constexpr bool ranges_overlap(uint64_t a, size_t len_a, uint64_t b, size_t len_b) {
    auto last_a = (a + len_a) - 1;
    auto last_b = (b + len_b) - 1;

    return !(last_b < a || last_a < b);
}

template<typename T> requires std::is_same_v<T, uint16_t> || std::is_same_v<T, uint32_t> || std::is_same_v<T, uint64_t>
constexpr T bswap(T v) {
    if constexpr(std::is_same_v<T, uint16_t>) return __builtin_bswap16(v);
    else if constexpr(std::is_same_v<T, uint32_t>) return __builtin_bswap32(v);
    else if constexpr(std::is_same_v<T, uint64_t>) return __builtin_bswap64(v);
}