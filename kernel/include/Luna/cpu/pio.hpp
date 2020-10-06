#pragma once

#include <Luna/common.hpp>

namespace pio
{
    uint8_t inb(uint16_t port);
    void outb(uint16_t port, uint8_t v);

    uint16_t inw(uint16_t port);
    void outw(uint16_t port, uint16_t v);

    uint32_t ind(uint16_t port);
    void outd(uint16_t port, uint32_t v);
} // namespace pio


