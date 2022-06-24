#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

#include <Luna/misc/log.hpp>

namespace vm::irqs::pic {
    constexpr uint16_t master_base = 0x20;
    constexpr uint16_t slave_base = 0xA0;

    constexpr uint16_t cmd = 0x0;
    constexpr uint16_t data = 0x1;
    
    constexpr uint8_t icw1_icw4 = 0x1;
    constexpr uint8_t icw1_init = 0x10;

    constexpr uint16_t elcr_master = 0x4D0;
    constexpr uint16_t elcr_slave = 0x4D1;

    struct Driver final : public vm::AbstractPIODriver, public vm::AbstractIRQListener {
        Driver(Vm* vm): vm{vm} {
            pics[0].elcr_mask = 0xF8;
            pics[1].elcr_mask = 0xDE;

            vm->pio_map[master_base + cmd] = this;
            vm->pio_map[master_base + data] = this;

            vm->pio_map[slave_base + cmd] = this;
            vm->pio_map[slave_base + data] = this;

            vm->pio_map[elcr_master] = this;
            vm->pio_map[elcr_slave] = this;
        }

        void pio_write(uint16_t port, uint32_t value, uint8_t size) {
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
                        if(value & 0x2)
                            dev.reg_read_select = value & 1;
                        else
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
                        } else {
                            print("pic: Unknown CMD {:#x}\n", value);
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

        uint32_t pio_read(uint16_t port, uint8_t size) {
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

        void irq_set(uint8_t irq, bool level) {
            ASSERT(irq < 16);

            set_irq1(irq >> 3, irq & 7, level);
            update_irq();
        }

        private:
        void inject_irq(int device, uint8_t irq) {
            print("pic: Raising IRQ{}\n", irq + device * 8);
            vm->cpus[0].vcpu->inject_int(vm::AbstractVm::InjectType::ExtInt, pics[device].vector + irq); // PIC always sends IRQs to CPU 0
        }

        uint8_t get_priority(int dev, uint8_t mask) {
            if(mask == 0)
                return 8;

            uint8_t priority = 0;
            while((mask & (1 << ((priority + pics[dev].priority_add) & 7))) == 0)
                priority++;

            return priority;
        }

        int get_irq(int device) {
            auto& dev = pics[device];
            auto mask = dev.irr & ~dev.imr;
            auto priority = get_priority(device, mask);
            if(priority == 8)
                return -1;

            mask = dev.isr;
            auto cur_priority = get_priority(device, mask);
            if(priority < cur_priority)
                return (priority + dev.priority_add) & 7;
            else
                return -1;
        }

        void update_irq() {    
            auto irq2 = get_irq(1);
            if(irq2 >= 0) {
                ack(1, irq2);
                inject_irq(1, irq2); // TODO: Emulate cascade irq correctly
            }

            auto irq = get_irq(0);
            if(irq >= 0) {
                ack(0, irq);
                inject_irq(0, irq);
            }
        }

        int set_irq1(int device, uint8_t irq, bool level) {
            int ret = 1;
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

            return (dev.imr & mask) ? -1 : ret;
        }

        void ack(int device, uint8_t irq) {
            auto& dev = pics[device];
            dev.isr |= (1 << irq);

            if(!(dev.elcr & (1 << irq)))
                dev.irr &= ~(1 << irq);
        }

        struct {
            uint8_t vector;
            bool icw4;
            uint8_t init_state, reg_read_select, priority_add = 0, irr, imr, isr;
            uint8_t elcr, elcr_mask, last_irr;
        } pics[2];
        vm::Vm* vm;
    };
} // namespace vm::irqs::pic
