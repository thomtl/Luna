#pragma once

#include <Luna/common.hpp>
#include <Luna/misc/stivale2.hpp>
#include <std/span.hpp>

namespace pmm
{
    constexpr size_t block_size = 0x1000;

    void init(stivale2::Parser& parser);
    uintptr_t alloc_block();
    void free_block(uintptr_t block);
    void reserve_block(uintptr_t block);
} // namespace pmm