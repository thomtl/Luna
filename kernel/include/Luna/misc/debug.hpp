#pragma once

#include <Luna/common.hpp>

namespace debug {
    void trace_stack(uintptr_t rbp);
    void trace_stack();

    constexpr uint8_t poison_exec = 0;
    constexpr uint8_t poison_write = 0b01;
    constexpr uint8_t poison_rw = 0b11;
    
    constexpr uint8_t poison_byte = 0 << 2;
    constexpr uint8_t poison_word = 0b01 << 2;
    constexpr uint8_t poison_qword = 0b10 << 2;
    constexpr uint8_t poison_dword = 0b11 << 2;
    
    void poison_addr(uint8_t n, uintptr_t addr, uint8_t flags);
} // namespace debug
