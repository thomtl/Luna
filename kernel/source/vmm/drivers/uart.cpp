#include <Luna/vmm/drivers/uart.hpp>

using namespace vm::uart;

Driver::Driver(Vm* vm, uint16_t base, log::Logger* logger): base{base}, baud{3}, dlab{false}, logger{logger} {
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

void Driver::pio_write(uint16_t port, uint32_t value, uint8_t size) {
    DEBUG_ASSERT(size == 1);

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

uint32_t Driver::pio_read(uint16_t port, uint8_t size) {
    DEBUG_ASSERT(size == 1);
    
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