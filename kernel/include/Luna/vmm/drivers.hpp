#pragma once

#include <Luna/common.hpp>
#include <Luna/gui/gui.hpp>

namespace vm {
    struct Vm;

    struct AbstractPIODriver {
        virtual ~AbstractPIODriver() {}

        virtual void pio_write(uint16_t port, uint32_t value, uint8_t size) = 0;
        virtual uint32_t pio_read(uint16_t port, uint8_t size) = 0;
    };

    struct AbstractMMIODriver {
        virtual ~AbstractMMIODriver() {}

        AbstractMMIODriver() = default;
        AbstractMMIODriver(const vm::AbstractMMIODriver&) = default;

        virtual void mmio_write(uintptr_t addr, uint64_t value, uint8_t size) = 0;
        virtual uint64_t mmio_read(uintptr_t addr, uint8_t size) = 0;
    };

    struct AbstractIRQListener {
        virtual ~AbstractIRQListener() {}

        virtual void irq_set(uint8_t vector, bool level) = 0;
        virtual bool read_irq_pin() = 0;
        virtual uint8_t read_irq_vector() = 0;
    };

    struct AbstractKeyboardListener {
        virtual ~AbstractKeyboardListener() {}

        virtual void handle_key_op(gui::KeyOp op, gui::KeyCodes code) = 0;
    };
} // namespace vm
