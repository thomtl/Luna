#pragma once

#include <stdint.h>
#include <stddef.h>

#include <Luna/misc/misc.hpp>
#include <Luna/cpp_support.hpp>

#define PANIC(msg) panic(__FILE__, __PRETTY_FUNCTION__, __LINE__, msg)

#define ASSERT(expr) \
        if(!(expr)) \
            PANIC("Assertion " #expr " Failed")

extern uintptr_t phys_mem_map;
constexpr uintptr_t kernel_vbase = 0xFFFF'FFFF'8000'0000;

