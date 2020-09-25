#pragma once

#include <Luna/common.hpp>

namespace pio
{
    uint8_t inb(uint16_t port);
    void outb(uint16_t port, uint8_t v);
} // namespace pio


