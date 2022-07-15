#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

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
        Driver(Vm* vm);

        void pio_write(uint16_t port, uint32_t value, uint8_t size);
        uint32_t pio_read(uint16_t port, uint8_t size);

        void irq_set(uint8_t irq, bool level) override;

        private:
        uint8_t get_priority(uint8_t device, uint8_t mask);
        std::optional<uint8_t> get_irq(uint8_t device);
        std::optional<bool> set_irq1(uint8_t device, uint8_t irq, bool level);
        void ack(uint8_t device, uint8_t irq);
        void update_irq();

        bool read_irq_pin() override { return irq_output; }
        uint8_t read_irq_vector() override;

        struct {
            uint8_t vector;
            bool icw4;
            uint8_t init_state, reg_read_select, priority_add = 0, irr, imr, isr;
            uint8_t elcr, elcr_mask, last_irr;
        } pics[2];
        vm::Vm* vm;

        bool irq_output;
    };
} // namespace vm::irqs::pic
