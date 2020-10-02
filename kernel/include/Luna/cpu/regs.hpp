#pragma once

#include <Luna/common.hpp>

namespace msr
{
    constexpr uint32_t gs_base = 0xC0000101;

    uint64_t read(uint32_t msr);
    void write(uint32_t msr, uint64_t v);
} // namespace msr

namespace cr4
{
    uint64_t read();
    void write(uint64_t v);
} // namespace cr4
