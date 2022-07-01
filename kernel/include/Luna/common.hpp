#pragma once

#include <stdint.h>
#include <stddef.h>

#include <Luna/misc/misc.hpp>
#include <Luna/cpp_support.hpp>

#define PANIC(msg) panic(__FILE__, __PRETTY_FUNCTION__, __LINE__, msg)

#define ASSERT(expr) do { \
        if(!(expr)) \
            PANIC("Assertion " #expr " Failed"); \
    } while(0)

// TODO: Remove in release mode
#define DEBUG_ASSERT(expr) ASSERT(expr)

extern uintptr_t phys_mem_map;
constexpr uintptr_t kernel_vbase = 0xFFFF'FFFF'8000'0000;

