#pragma once

#include <Luna/common.hpp>

namespace debug {
    void trace_stack(uintptr_t rbp);
    void trace_stack();
} // namespace debug
