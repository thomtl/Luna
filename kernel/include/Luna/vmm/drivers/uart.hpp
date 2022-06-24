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
        Driver(Vm* vm, uint16_t base, log::Logger* logger): base{base}, baud{3}, dlab{false}, logger{logger} {
            vm->pio_map[base + data_reg] = this;
            vm->pio_map[base + irq_enable_reg] = this;
            vm->pio_map[base + fifo_control_reg] = this;
            vm->pio_map[base + line_control_reg] = this;
            vm->pio_map[base + modem_control_reg] = this;
            vm->pio_map[base + line_status_reg] = this;
            vm->pio_map[base + modem_status_reg] = this;
            vm->pio_map[base + scratch_reg] = this;

            iir = 2;
        }

        void pio_write(uint16_t port, uint32_t value, [[maybe_unused]] uint8_t size) {
            if(port == (base + data_reg)) {
                if(!dlab) {
                    logger->putc(value);

                    if(value == '\n')
                        logger->flush();
                } else {
                    baud &= ~0xFF;
                    baud |= value;
                }
            } else if(port == (base + irq_enable_reg)) {
                if(!dlab) {
                    ier = value;
                } else {
                    baud &= ~0xFF00;
                    baud |= (value << 8);
                }
            } else if(port == (base + fifo_control_reg)) {
                fifo_control = value;
            } else if(port == (base + modem_control_reg)) {
                mcr = value;

                ASSERT(!(mcr & (1 << 4)));
            } else if(port == (base + line_control_reg)) {
                auto new_dlab = (value >> 7) & 1;
                if(dlab && !new_dlab)
                    print("uart: New baudrate {:d}\n", clock / baud);

                dlab = new_dlab;

                /*char parity = 'U';
                switch ((value >> 3) & 0b111) {
                    case 0b000: parity = 'N'; break;
                    case 0b001: parity = 'O'; break;
                    case 0b011: parity = 'E'; break;
                }
                print("uart: Set config {}{}{}\n", (value & 0b11) + 5, parity, (value & (1 << 2)) ? 2 : 1);*/
            } else if(port == (base + scratch_reg)) {
                scratchpad = value;
            } else {
                print("uart: Unhandled write to reg {} (Port: {:#x}): {:#x}\n", port - base, port, value);
            }
        }

        uint32_t pio_read(uint16_t port, [[maybe_unused]] uint8_t size) {
            if(port == (base + data_reg)) {
                if(!dlab)
                    return ' '; // Just send spaces for now
                else
                    return baud & 0xFF;
            } else if(port == (base + irq_enable_reg)) {
                if(!dlab)
                    return ier;
                else
                    return (baud >> 8) & 0xFF;
            } else if(port == (base + irq_identification_reg)) {
                return iir;
            } else if(port == (base + line_control_reg)) {
                return (dlab << 7);
            } else if(port == (base + modem_control_reg)) {
                return mcr;
            } else if(port == (base + line_status_reg)) {
                return (1 << 6) | (1 << 5); // Transmitter Idle, can send bits
            } else if(port == (base + modem_status_reg)) {
                return 0; // TODO
            } else if(port == (base + scratch_reg)) {
                return scratchpad;
            }

            print("uart: Unhandled read from reg {} (Port: {:#x})\n", port - base, port);
            return 0;
        }

        private:
        uint16_t base;
        uint8_t ier, iir, mcr, fifo_control;
        uint8_t scratchpad;

        uint16_t baud;

        bool dlab;

        log::Logger* logger;
    };
} // namespace vm::uart
