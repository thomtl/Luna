#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

#include <Luna/gui/gui.hpp>

#include <Luna/misc/log.hpp>
#include <Luna/drivers/uart.hpp>

namespace vm::ps2 {
    constexpr uint8_t data = 0x60;
    constexpr uint8_t cmd = 0x64;

    constexpr uint8_t buffer_size = 16;

    constexpr uint8_t port_a_irq_line = 1;
    constexpr uint8_t port_b_irq_line = 12;

    struct Driver final : public vm::AbstractPIODriver, public vm::AbstractKeyboardListener {
        Driver(Vm* vm);

        void handle_key_op(gui::KeyOp op, gui::KeyCodes code) override;    

        void pio_write(uint16_t port, uint32_t value, uint8_t size);
        uint32_t pio_read(uint16_t port, uint8_t size);

        private:
        struct Port;

        void handle_command(uint8_t cmd);
        void finish_multibyte_cmd();

        void port_send(Port& port);
        void finish_multibyte_port_cmd(Port& port);

        void push_obf(uint8_t v);
        void push_ibf(uint8_t v);
        uint8_t pop_ibf();

        void inject_irq(Port& port);

        vm::Vm* vm;

        uint8_t multibyte_cmd = 0, multibyte_n;

        uint8_t out_buffer[buffer_size], out_i = 0;
        uint8_t in_buffer[buffer_size], in_i = 0;

        uint8_t ram[32];

        bool obf = false, ibf = false;

        struct Port {
            bool enabled, irq_enable, clock, translate, scanning;
            uint8_t scancode_set = 2;
            uint8_t irq_line;

            uint8_t multibyte_cmd = 0, multibyte_n = 0;
        } a, b;
    };
} // namespace vm::ps/2