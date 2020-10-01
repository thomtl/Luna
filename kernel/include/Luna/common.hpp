#pragma once

#include <stdint.h>
#include <stddef.h>

#include <Luna/misc/misc.hpp>

#define PANIC(msg) panic(__FILE__, __PRETTY_FUNCTION__, __LINE__, msg)

#define ASSERT(expr) \
        if(!(expr)) \
            PANIC("Assertion " #expr " Failed")

constexpr uintptr_t phys_mem_map = 0xFFFF'8000'0000'0000;

