#include <Luna/vmm/drivers/ps2.hpp>

/* This table was taken from https://elixir.bootlin.com/qemu/v7.0.0/source/hw/input/ps2.c#L130 with the license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
constexpr uint8_t controller_translate_table[256] = {
    0xff, 0x43, 0x41, 0x3f, 0x3d, 0x3b, 0x3c, 0x58,
    0x64, 0x44, 0x42, 0x40, 0x3e, 0x0f, 0x29, 0x59,
    0x65, 0x38, 0x2a, 0x70, 0x1d, 0x10, 0x02, 0x5a,
    0x66, 0x71, 0x2c, 0x1f, 0x1e, 0x11, 0x03, 0x5b,
    0x67, 0x2e, 0x2d, 0x20, 0x12, 0x05, 0x04, 0x5c,
    0x68, 0x39, 0x2f, 0x21, 0x14, 0x13, 0x06, 0x5d,
    0x69, 0x31, 0x30, 0x23, 0x22, 0x15, 0x07, 0x5e,
    0x6a, 0x72, 0x32, 0x24, 0x16, 0x08, 0x09, 0x5f,
    0x6b, 0x33, 0x25, 0x17, 0x18, 0x0b, 0x0a, 0x60,
    0x6c, 0x34, 0x35, 0x26, 0x27, 0x19, 0x0c, 0x61,
    0x6d, 0x73, 0x28, 0x74, 0x1a, 0x0d, 0x62, 0x6e,
    0x3a, 0x36, 0x1c, 0x1b, 0x75, 0x2b, 0x63, 0x76,
    0x55, 0x56, 0x77, 0x78, 0x79, 0x7a, 0x0e, 0x7b,
    0x7c, 0x4f, 0x7d, 0x4b, 0x47, 0x7e, 0x7f, 0x6f,
    0x52, 0x53, 0x50, 0x4c, 0x4d, 0x48, 0x01, 0x45,
    0x57, 0x4e, 0x51, 0x4a, 0x37, 0x49, 0x46, 0x54,
    0x80, 0x81, 0x82, 0x41, 0x54, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
    0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
    0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
    0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
    0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
    0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7,
    0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
    0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
    0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
    0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
};

using enum gui::KeyCodes;
#define _(x) static_cast<uint32_t>(x)
constexpr uint16_t keycode_to_set2[] = {
    [_(Unknown)] = 0,

    [_(A)] = 0x1C, [_(B)] = 0x32, [_(C)] = 0x21, [_(D)] = 0x23, [_(E)] = 0x24, [_(F)] = 0x2B, [_(G)] = 0x34,
    [_(H)] = 0x33, [_(I)] = 0x43, [_(J)] = 0x3B, [_(K)] = 0x42, [_(L)] = 0x4B, [_(M)] = 0x3A, [_(N)] = 0x31,
    [_(O)] = 0x44, [_(P)] = 0x4D, [_(Q)] = 0x15, [_(R)] = 0x2D, [_(S)] = 0x1B, [_(T)] = 0x2C, [_(U)] = 0x3C,
    [_(V)] = 0x2A, [_(W)] = 0x1D, [_(X)] = 0x22, [_(Y)] = 0x35, [_(Z)] = 0x1A,

    [_(_0)] = 0x45, [_(_1)] = 0x16, [_(_2)] = 0x1E, [_(_3)] = 0x26, [_(_4)] = 0x25,
    [_(_5)] = 0x2E, [_(_6)] = 0x36, [_(_7)] = 0x3D, [_(_8)] = 0x3E, [_(_9)] = 0x46,

    [_(Enter)] = 0x5A,
    [_(Tab)] = 0x0D,
    [_(Escape)] = 0x76,
    [_(Delete)] = 0xE071,
    [_(Space)] = 0x29,
    [_(BackSpace)] = 0x66,

    [_(Minus)] = 0x4E,
    [_(Plus)] = 0x0, // TODO
    [_(Star)] = 0x0, // TODO
    [_(Equals)] = 0x55,

    [_(BraceOpen)] = 0x54,
    [_(BraceClose)] = 0x5B,
    [_(ParenOpen)] = 0x0, // TODO
    [_(ParenClose)] = 0x0, // TODO
    [_(BackSlash)] = 0x5D,
    [_(Tilde)] = 0x0, // TODO
    [_(Semicolon)] = 0x4C,
    [_(Comma)] = 0x41,
    [_(Dot)] = 0x49,
    [_(Slash)] = 0x4A,

    [_(F1)] = 0x05, [_(F2)] = 0x06, [_(F3)] = 0x04, [_(F4)] = 0x0C, [_(F5)] = 0x03, [_(F6)] = 0x0B,
    [_(F7)] = 0x83, [_(F8)] = 0x0A, [_(F9)] = 0x01, [_(F10)] = 0x09, [_(F11)] = 0x78, [_(F12)] = 0x07,
    [_(F13)] = 0x0, // TODO
    [_(F14)] = 0x0, // TODO
    [_(F15)] = 0x0, // TODO
    [_(F16)] = 0x0, // TODO
    [_(F17)] = 0x0, // TODO
    [_(F18)] = 0x0, // TODO
    [_(F19)] = 0x0, // TODO
    [_(F20)] = 0x0, // TODO
    [_(F21)] = 0x0, // TODO
    [_(F22)] = 0x0, // TODO
    [_(F23)] = 0x0, // TODO
    [_(F24)] = 0x0, // TODO

    [_(CapsLock)] = 0x58,
    [_(NumLock)] = 0x77,
    [_(ScrollLock)] = 0x7E,
    [_(PrntScrn)] = 0x0, // TODO
    [_(Pause)] = 0x0, // TODO
    [_(Insert)] = 0xE070,
    [_(Home)] = 0xE06C,
    [_(PageUp)] = 0xE07D,

    [_(End)] = 0xE069,
    [_(PageDown)] = 0xE07A,
    [_(RightArrow)] = 0xE074,
    [_(LeftArrow)] = 0xE06B,
    [_(DownArrow)] = 0xE072,
    [_(UpArrow)] = 0xE075,

    [_(LeftShift)] = 0x12, [_(RightShift)] = 0x59,
    [_(LeftControl)] = 0x14, [_(RightControl)] = 0xE014,
    [_(LeftAlt)] = 0x11, [_(RightAlt)] = 0xE011,
    [_(LeftGUI)] = 0xE01F, [_(RightGUI)] = 0xE027,

};
#undef _

using namespace vm::ps2;

Driver::Driver(Vm* vm): vm{vm} {
    vm->pio_map[data] = this;
    vm->pio_map[cmd] = this;

    a.irq_line = port_a_irq_line;
    b.irq_line = port_b_irq_line;
}

void Driver::pio_write(uint16_t port, uint32_t value, uint8_t size) {
    ASSERT(size == 1);

    if(port == data) {
        push_ibf(value);

        if(multibyte_cmd && in_i == multibyte_n) {
            finish_multibyte_cmd();
        } else if(a.multibyte_cmd && in_i == a.multibyte_n) {
            finish_multibyte_port_cmd(a);
        } else {
            port_send(a);
        }
    } else if(port == cmd) {
        handle_command(value);
    } else  {
        PANIC("Unknown PS/2 Register");
    }
}

uint32_t Driver::pio_read(uint16_t port, uint8_t size) {
    ASSERT(size == 1);

    if(port == data) {
        if(out_i == 0) {
            print("ps2: Output buffer empty\n");
            return 0; // Output buffer empty
        }

        uint8_t ret = out_buffer[--out_i];
        if(out_i == 0)
            obf = false;
                
        return ret;
    } else if(port == cmd) {
        return (ibf << 1) | obf;
    } else {
        PANIC("Unknown PS/2 Register");
    }
}

void Driver::handle_key_op(gui::KeyOp op, gui::KeyCodes code) {
    bool need_unpress_marker = false;
    auto put_code = [&](uint8_t code) {
        if(a.translate) {
            if(code == 0xF0) {
                need_unpress_marker = true;
                return;
            }

            push_obf(controller_translate_table[code] | (need_unpress_marker << 7));
            need_unpress_marker = false;
        } else {
            push_obf(code);
        }

        inject_irq(a);
    };

    if(a.scancode_set == 2) {
        auto scancode = keycode_to_set2[static_cast<uint32_t>(code)];
        if(scancode == 0) {
            print("vm/ps2: Unknown KeyCode: {:#x}\n", static_cast<uint32_t>(code));
            return;
        }
        
        if(auto high = (scancode >> 8) & 0xFF; high)
            put_code(high);

        if(op == gui::KeyOp::Unpress)
            put_code(0xF0);
            
        put_code(scancode & 0xFF);
    } else {
        print("vm/ps2: Unknown Scancode Set {}\n", a.scancode_set);
    }
}    

void Driver::handle_command(uint8_t cmd) {
    switch(cmd) {
        case 0x20 ... 0x3F: // Read from RAM
            push_obf(ram[cmd & 0x1F]);
            inject_irq(a);
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
            inject_irq(a);
            break;
        case 0xAB: // Test first PS/2 port
            push_obf(0); // Test Passed
            inject_irq(a);
            break;
        case 0xA9: // Test second PS/2 port
            push_obf(0); // Test Passed
            inject_irq(a);
            break;
        case 0xAD: // Disable first PS/2 port
            a.enabled = false;
            break;
        case 0xAE: // Enable first PS/2 port
            a.enabled = true;
            break;
        case 0xD3: // Write Aux obf
            multibyte_cmd = cmd;
            multibyte_n = 1;
            break;
        default:
            print("ps/2: Unknown command: {:#x}\n", cmd);
            PANIC("Unknown PS/2 command");
    }
}

void Driver::finish_multibyte_cmd() {
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
    } else if(multibyte_cmd == 0xD3) {
        push_obf(pop_ibf()); // What part of this has to do with aux?
        inject_irq(a);
    } else {
        print("ps/2: Unknown multibyte command: {:#x}\n", multibyte_cmd);
    }

    multibyte_cmd = 0;
    multibyte_n = 0;
}

void Driver::port_send(Port& port) {
    uint8_t cmd = pop_ibf();

    switch(cmd) {
        case 0xFF: // Reset
            push_obf(0xAA); // Success
            push_obf(0xFA); // Ack
            inject_irq(port);
            break;
        case 0xF5: // Disable Scanning
            port.scanning = false;

            push_obf(0xFA);
            inject_irq(port);
            break;
        case 0xF4: // Enable Scanning
            port.scanning = true;

            push_obf(0xFA);
            inject_irq(port);
            break;
        case 0xF3: // Set typematic rate and delay:
            port.multibyte_cmd = cmd;
            port.multibyte_n = 1;

            push_obf(0xFA);
            inject_irq(port);
            break;
        case 0xF2:
            push_obf(0x83); // MF2 keyboard
            push_obf(0xAB);

            push_obf(0xFA);
            inject_irq(port);
            break;
        case 0xF0: // Set Scancode Set
            port.multibyte_cmd = cmd;
            port.multibyte_n = 1;  

            push_obf(0xFA);
            inject_irq(port);
            break;
        case 0xED:
            port.multibyte_cmd = cmd;
            port.multibyte_n = 1;  

            push_obf(0xFA);
            inject_irq(port);
            break;
        default:
            print("ps/2: Unknown port cmd: {:#x}\n", cmd);
            PANIC("Unknown cmd");
            break;
    }
}

void Driver::finish_multibyte_port_cmd(Port& port) {
    if(port.multibyte_cmd == 0xF3) {
        auto v = pop_ibf();

        (void)v; // Don't care

        push_obf(0xFA);
        inject_irq(port);
    } else if(port.multibyte_cmd == 0xF0) {
        auto v = pop_ibf();

        if(v == 0) { // Get current scancode set
            push_obf(port.translate ? controller_translate_table[port.scancode_set] : port.scancode_set);
            push_obf(0xFA);
            inject_irq(port);
        } else {
            port.scancode_set = v;
            push_obf(0xFA);
            inject_irq(port);
        }
    } else if(port.multibyte_cmd == 0xED) {
        pop_ibf(); // LED states

        push_obf(0xFA);
        inject_irq(port);
    } else {
        print("ps/2: Unknown multibyte port cmd: {:#x}\n", port.multibyte_cmd);
        PANIC("Unknown cmd");
    }

    port.multibyte_cmd = 0;
    port.multibyte_n = 0;
}

void Driver::push_obf(uint8_t v) {
    out_buffer[out_i++] = v;
    if(out_i == buffer_size)
        out_i = 0;

    obf = true;
}

void Driver::push_ibf(uint8_t v) {
    if(ibf)
        return;
            
    in_buffer[in_i++] = v;
    if(in_i == buffer_size)
        ibf = true;
}

uint8_t Driver::pop_ibf() {
    if(in_i == 0)
        return 0;

    uint8_t v = in_buffer[--in_i];
    ibf = false; // We just popped something so its not full anymore

    return v;
}

void Driver::inject_irq(Port& port) {
    if(!port.irq_enable)
        return;

    vm->set_irq(port.irq_line, true);
    vm->set_irq(port.irq_line, false);
}