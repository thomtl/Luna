#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

#include <Luna/misc/log.hpp>

namespace vm::uart {
    constexpr uint32_t clock = 115200;

    constexpr uint8_t data_reg = 0;
    constexpr uint8_t irq_enable_reg = 1;
    constexpr uint8_t irq_identification_reg = 2;
    constexpr uint8_t fifo_control_reg = 2;
    constexpr uint8_t line_control_reg = 3;
    constexpr uint8_t modem_control_reg = 4;
    constexpr uint8_t line_status_reg = 5;
    constexpr uint8_t modem_status_reg = 6;
    constexpr uint8_t scratch_reg = 7;

    struct Driver final : public vm::AbstractPIODriver {
        Driver(Vm* vm, uint16_t base, log::Logger* logger);

        void pio_write(uint16_t port, uint32_t value, uint8_t size);
        uint32_t pio_read(uint16_t port, uint8_t size);

        private:
        uint16_t base;
        uint8_t ier, iir, mcr, fifo_control;
        uint8_t scratchpad;

        uint16_t baud;

        bool dlab;

        log::Logger* logger;
    };
} // namespace vm::uart
