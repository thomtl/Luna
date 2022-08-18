#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

#include <Luna/drivers/timers/timers.hpp>
#include <Luna/misc/log.hpp>

namespace vm::pit {
    constexpr uint16_t ch0_data = 0x40;
    constexpr uint16_t ch1_data = 0x41;
    constexpr uint16_t ch2_data = 0x42;
    constexpr uint16_t cmd = 0x43;

    constexpr uint16_t channel2_status = 0x61;

    constexpr uint64_t clock_frequency = 1193182;
    constexpr uint32_t irq_line = 0;

    struct Driver final : public vm::AbstractPIODriver {
        Driver(Vm* vm);

        Driver(Driver&&) = delete;
        Driver(const Driver&) = delete;
        Driver& operator=(Driver&&) = delete;
        Driver& operator=(const Driver&) = delete;

        void pio_write(uint16_t port, uint32_t value, uint8_t size);
        uint32_t pio_read(uint16_t port, uint8_t size);

        private:
        void irq_handler();

        struct Channel;

        void setup_channel(uint8_t ch);
        bool get_channel_output(uint8_t ch);
        uint16_t get_channel_count(uint8_t ch);
        uint64_t get_tick(uint8_t ch);

        enum class AccessMode { FlipFlopLow, FlipFlopHigh };

        struct Channel {
            uint64_t start_tick;
            uint32_t count;

            uint8_t mode;
            uint8_t count_latch;

            bool gate;

            AccessMode write_state, read_state;
        } channels[3] = {};

        timer::Timer ch0_timer;

        Vm* vm;
    };
} // namespace vm::pit
