#pragma once

#include <Luna/common.hpp>
#include <Luna/misc/stivale2.hpp>

namespace pmm
{
    void init(stivale2::Parser& parser);
    uintptr_t alloc_block();
    void free_block(uintptr_t block);
} // namespace pmm