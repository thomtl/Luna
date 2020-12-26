#pragma once

#include <Luna/common.hpp>

namespace vm {
    struct Vm;

    struct AbstractPIODriver {
        virtual ~AbstractPIODriver() {}

        virtual void register_pio_driver(Vm* vm) = 0;

        virtual void pio_write(uint16_t port, uint32_t value, uint8_t size) = 0;
        virtual uint32_t pio_read(uint16_t port, uint8_t size) = 0;
    };

    struct AbstractMMIODriver {
        virtual ~AbstractMMIODriver() {}

        AbstractMMIODriver() = default;
        AbstractMMIODriver(const vm::AbstractMMIODriver&) = default;

        virtual void register_mmio_driver(Vm* vm) = 0;

        virtual void mmio_write(uintptr_t addr, uint64_t value, uint8_t size) = 0;
        virtual uint64_t mmio_read(uintptr_t addr, uint8_t size) = 0;
    };
} // namespace vm
