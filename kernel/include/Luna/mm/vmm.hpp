#pragma once

#include <Luna/common.hpp>
#include <Luna/cpu/paging.hpp>

namespace vmm
{
    void init_bsp();
    bool is_canonical(uintptr_t addr);
    paging::Context create_context();

    paging::Context& get_kernel_context();
} // namespace vmm
