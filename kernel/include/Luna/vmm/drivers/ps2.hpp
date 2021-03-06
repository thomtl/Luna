#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

#include <Luna/misc/log.hpp>
#include <Luna/drivers/uart.hpp>

namespace vm::ps2 {
    constexpr uint8_t data = 0x60;
    constexpr uint8_t cmd = 0x64;

    constexpr uint8_t buffer_size = 16;

    struct Driver : public vm::AbstractPIODriver {
        Driver(Vm* vm) {
            vm->pio_map[data] = this;
            vm->pio_map[cmd] = this;
        }

        void pio_write(uint16_t port, uint32_t value, uint8_t size) {
            ASSERT(size == 1);

            if(port == data) {
                push_ibf(value);

                if(multibyte_cmd && in_i == multibyte_n)
                    finish_multibyte_cmd();
                else if(a.multibyte_cmd && in_i == a.multibyte_n)
                    finish_multibyte_port_cmd(a);
                else
                    port_send(a);
            } else if(port == cmd) {
                handle_command(value);
            } else 
                PANIC("Unknown PS/2 Register");
        }

        uint32_t pio_read(uint16_t port, uint8_t size) {
            ASSERT(size == 1);

            if(port == data) {
                if(out_i == 0)
                    return 0; // Output buffer empty

                uint8_t ret = out_buffer[--out_i];
                if(out_i == 0)
                    obf = false;
                
                return ret;
            } else if(port == cmd) {
                return (ibf << 1) | obf;
            } else 
                PANIC("Unknown PS/2 Register");
        }

        void handle_command(uint8_t cmd) {
            switch(cmd) {
                case 0x20 ... 0x3F: // Read from RAM
                    push_obf(ram[cmd & 0x1F]);
                    break;
                case 0x60 ... 0x7F: // Write to RAM
                    multibyte_cmd = cmd;
                    multibyte_n = 1;
                    return;
                case 0xA7: // Disable second PS/2 port
                    b.enabled = false;
                    break;
                case 0xA8: // Enable second PS/2 port
                    b.enabled = true;
                    break;
                case 0xAA: // Test PS/2 controller
                    push_obf(0x55); // Test passed
                    break;
                case 0xAB: // Test first PS/2 port
                    push_obf(0); // Test Passed
                    break;
                case 0xA9: // Test second PS/2 port
                    push_obf(0); // Test Passed
                    break;
                case 0xAD: // Disable first PS/2 port
                    a.enabled = false;
                    break;
                case 0xAE: // Enable first PS/2 port
                    a.enabled = true;
                    break;
                default:
                    print("ps/2: Unknown command: {:#x}\n", cmd);
                    PANIC("Unknown PS/2 command");
            }
        }

        void finish_multibyte_cmd() {
            if(multibyte_cmd >= 0x60 && multibyte_cmd <= 0x7F) {
                uint8_t i = multibyte_cmd & 0x1F;
                ram[i] = pop_ibf();

                if(i == 0) { // Controller config byte
                    a.irq_enable = (ram[i] >> 0) & 1;
                    b.irq_enable = (ram[i] >> 1) & 1;

                    a.clock = !((ram[i] >> 4) & 1);
                    b.clock = !((ram[i] >> 5) & 1);

                    a.translate = (ram[i] >> 6) & 1;
                }
            }

            multibyte_cmd = 0;
            multibyte_n = 0;
        }

        struct Port;

        void port_send(Port& port) {
            uint8_t cmd = pop_ibf();

            switch(cmd) {
                case 0xFF: // Reset
                    push_obf(0xAA); // Success
                    push_obf(0xFA); // Ack
                    break;
                case 0xF5: // Disable Scanning
                    port.scanning = false;

                    push_obf(0xFA);
                    break;
                case 0xF4: // Enable Scanning
                    port.scanning = true;

                    push_obf(0xFA);
                    break;
                case 0xF2:
                    push_obf(0x83); // MF2 keyboard
                    push_obf(0xAB);

                    push_obf(0xFA);
                    break;
                case 0xF0: // Set Scancode Set
                    port.multibyte_cmd = cmd;
                    port.multibyte_n = 1;  

                    push_obf(0xFA);
                    break;
                case 0xED:
                    port.multibyte_cmd = cmd;
                    port.multibyte_n = 1;  

                    push_obf(0xFA);
                    break;
                default:
                    print("ps/2: Unknown port cmd: {:#x}\n", cmd);
                    PANIC("Unknown cmd");
                    break;
            }
        }

        void finish_multibyte_port_cmd(Port& port) {
            if(port.multibyte_cmd == 0xF0) {
                auto v = pop_ibf();

                if(v == 0) { // Get current scancode set
                    push_obf(0xFA);
                    push_obf(port.scancode_set);
                } else {
                    port.scancode_set = v;
                    push_obf(0xFA);
                }
            } else if(port.multibyte_cmd == 0xED) {
                pop_ibf(); // LED states

                push_obf(0xFA);
            } else {
                print("ps/2: Unknown multibyte port cmd: {:#x}\n", port.multibyte_cmd);
                PANIC("Unknown cmd");
            }

            port.multibyte_cmd = 0;
            port.multibyte_n = 0;
        }

        void push_obf(uint8_t v) {
            out_buffer[out_i++] = v;
            if(out_i == buffer_size)
                out_i--;

            obf = true;
        }

        void push_ibf(uint8_t v) {
            if(ibf)
                return;
            
            in_buffer[in_i++] = v;
            if(in_i == buffer_size)
                ibf = true;
        }

        uint8_t pop_ibf() {
            if(in_i == 0)
                return 0;

            uint8_t v = in_buffer[--in_i];
            ibf = false; // We just popped something so its not full anymore

            return v;
        }

        uint8_t multibyte_cmd = 0, multibyte_n;

        uint8_t out_buffer[buffer_size], out_i = 0;
        uint8_t in_buffer[buffer_size], in_i = 0;

        uint8_t ram[32];

        bool obf = false, ibf = false;

        struct Port {
            bool enabled, irq_enable, clock, translate, scanning;
            uint8_t scancode_set = 2;

            uint8_t multibyte_cmd = 0, multibyte_n = 0;
        } a, b;
    };
} // namespace vm::ps/2