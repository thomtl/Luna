#include <Luna/vmm/drivers/irqs/pic.hpp>
#include <Luna/misc/log.hpp>

using namespace vm::irqs::pic;

Driver::Driver(Vm* vm): vm{vm} {
    pics[0].elcr_mask = 0xF8;
    pics[1].elcr_mask = 0xDE;

    vm->pio_map[master_base + cmd] = this;
    vm->pio_map[master_base + data] = this;

    vm->pio_map[slave_base + cmd] = this;
    vm->pio_map[slave_base + data] = this;

    vm->pio_map[elcr_master] = this;
    vm->pio_map[elcr_slave] = this;
}

void Driver::pio_write(uint16_t port, uint32_t value, uint8_t size) {
    ASSERT(size == 1);

    if(port == (master_base + cmd) || port == (master_base + data) || port == (slave_base + cmd) || port == (slave_base + data)) {
        auto reg = port & 1;
        auto master = port >> 7;
        auto& dev = pics[master];

        if(reg == cmd) {
            if(value & icw1_init) {
                auto edge_irr = dev.irr & ~dev.elcr;
                dev.icw4 = value & icw1_icw4;
                dev.imr = 0;
                dev.last_irr = 0;
                dev.irr &= dev.elcr;
                dev.init_state = 1;

                for(size_t i = 0; i < 8; i++)
                    if(edge_irr & (1 << i))
                        dev.isr &= ~(1 << i);

                if(value != (icw1_init | icw1_icw4))
                    print("pic: Unknown PIC init command: {:#x}\n", value);
            } else if(value & 0x8) {
                if(value & 0x2) {
                    dev.reg_read_select = value & 1;

                    value &= ~0x2;
                }
                        
                if(value)
                    print("pic: Unknown PIC OCW3 command: {:#x}\n", value);
            } else {
                auto cmd = value >> 5;
                if(cmd == 1 || cmd == 5) {
                    auto priority = get_priority(master, dev.isr);
                    if(priority != 8) {
                        auto irq = (priority + dev.priority_add) & 7;
                        if(cmd == 5)
                            dev.priority_add = (irq + 1) & 7;

                        dev.isr &= ~(1 << irq);
                        update_irq();
                    }
                } else if(cmd == 3) {
                    auto irq = value & 7;
                    dev.isr &= ~(1 << irq);
                    update_irq();
                } else {
                    print("pic: Unknown CMD {:#x}\n", cmd);
                }
            }
        } else if(reg == data) {
            if(dev.init_state == 0) {
                dev.imr = value;

                update_irq();
            } else if(dev.init_state == 1) { // Init ICW2 = Vector base
                dev.vector = value & 0xF8;
                dev.init_state = 2;

                print("pic: {} vector: {:#x}\n", master ? "Slave" : "Master", (uint16_t)dev.vector);
            } else if(dev.init_state == 2) {
                if(dev.icw4)
                    dev.init_state = 3;
                else
                    dev.init_state = 0;
            } else if(dev.init_state == 3) {
                if(value != 1)
                    print("pic: Unknown PIC ICW4 command: {:#x}\n", value);

                dev.init_state = 0;
            }
        }
    } else if(port == elcr_master || port == elcr_slave) {
        auto master = port & 1;
        auto& dev = pics[master];

        dev.elcr = value & dev.elcr_mask;
    } else {
        print("pic: Unhandled PIC write: {:#x} <- {:#x}\n", port, value);
    }
}

uint32_t Driver::pio_read(uint16_t port, uint8_t size) {
    ASSERT(size == 1);

    if(port == (master_base + cmd) || port == (master_base + data) || port == (slave_base + cmd) || port == (slave_base + data)) {
        auto reg = port & 1;
        auto master = ((port & ~1) == 0x20) ? 0 : 1;
        auto& dev = pics[master];

        if(reg == cmd) {
            if(dev.reg_read_select)
                return dev.isr;
            else
                return dev.irr;
        } else if(reg == data) {
            return dev.imr;
        } else {
            PANIC("Unreachable");
        }
    } else if(port == elcr_master || port == elcr_slave) {
        auto master = port & 1;
        auto& dev = pics[master];

        return dev.elcr;
    } else {
        print("pic: Unhandled PIC read: {:#x}\n", port);
        return 0;
    }
}

void Driver::irq_set(uint8_t irq, bool level) {
    ASSERT(irq < 16);

    set_irq1(irq >> 3, irq & 7, level);
    update_irq();
}

uint8_t Driver::get_priority(uint8_t device, uint8_t mask) {
    if(mask == 0)
        return 8;

    uint8_t priority = 0;
    while((mask & (1 << ((priority + pics[device].priority_add) & 7))) == 0)
        priority++;

    return priority;
}

std::optional<uint8_t> Driver::get_irq(uint8_t device) {
    auto& dev = pics[device];
    auto mask = dev.irr & ~dev.imr;
    auto priority = get_priority(device, mask);
    if(priority == 8)
        return std::nullopt;

    mask = dev.isr;
    auto cur_priority = get_priority(device, mask);
    if(priority < cur_priority)
        return (priority + dev.priority_add) & 7;
    else
        return std::nullopt;
}

void Driver::update_irq() {    
    auto irq2 = get_irq(1);
    if(irq2.has_value()) {
        set_irq1(0, 2, 1);
        set_irq1(0, 2, 0);
    }
    auto irq1 = get_irq(0);

    irq_output = irq1.has_value();
}

std::optional<bool> Driver::set_irq1(uint8_t device, uint8_t irq, bool level) {
    bool ret = true;
    auto mask = 1 << irq;
    auto& dev = pics[device];
    if(dev.elcr & mask) {
        if(level) {
            ret = !(dev.irr & mask);
            dev.irr |= mask;
            dev.last_irr |= mask;
        } else {
            dev.irr &= ~mask;
            dev.last_irr &= ~mask;
        }
    } else {
        if(level) {
            if((dev.last_irr & mask) == 0) {
                ret = !(dev.irr & mask);
                dev.irr |= mask;
            }
            dev.last_irr |= mask;
        } else {
            dev.last_irr &= ~mask;
        }
    }

    if(dev.imr & mask)
        return std::nullopt;
    else
        return ret;
}

void Driver::ack(uint8_t device, uint8_t irq) {
    auto& dev = pics[device];
    dev.isr |= (1 << irq);

    if(!(dev.elcr & (1 << irq)))
        dev.irr &= ~(1 << irq);
}

uint8_t Driver::read_irq_vector() {    
    uint8_t ret = 0;

    auto irq = get_irq(0);
    if(irq.has_value()) {
        ack(0, *irq);
        if(*irq == 2) {
            auto irq2 = get_irq(1);
            if(irq2.has_value())
                ack(1, *irq2);
            else
                irq2 = 7; // Slave Spurious IRQ

            ret = pics[1].vector + *irq2;
        } else {
            ret = pics[0].vector + *irq;
        }
    } else {
        print("pic: Spurious IRQ\n");
        ret = pics[0].vector + 7;
    }
    update_irq();

    return ret;
}