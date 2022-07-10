#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

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
        Driver(Vm* vm): vm{vm} {
            vm->pio_map[ch0_data] = this;
            vm->pio_map[ch1_data] = this;
            vm->pio_map[ch2_data] = this;
            vm->pio_map[cmd] = this;

            vm->pio_map[channel2_status] = this;
        }

        void pio_write(uint16_t port, uint32_t value, uint8_t size) {
            ASSERT(size == 1);
            ASSERT((value & ~0xFF) == 0);

            if(port == ch0_data || port == ch1_data || port == ch2_data) {
                auto i = port - ch0_data;
                auto& ch = channels[i];

                using enum AccessMode;
                if(ch.write_state == FlipFlopLow) {
                    ch.count_latch = value;
                    ch.write_state = FlipFlopHigh;
                } else if(ch.write_state == FlipFlopHigh) {
                    ch.count = ch.count_latch | (value << 8);
                    ch.write_state = FlipFlopLow;

                    setup_channel(i);
                } else 
                    PANIC("TODO");
            } else if(port == cmd) {
                auto ch = (value >> 6) & 0x3;
                auto access = (value >> 4) & 0x3;
                auto mode = (value >> 1) & 0x7;
                auto bcd = value & 1;

                ASSERT(!bcd); // TODO: BCD mode
                ASSERT(access == 0x3); // TODO: Other access modes
                ASSERT(mode == 0 || mode == 2 || mode == 4);

                ASSERT(ch != 1);
                channels[ch].mode = mode;
                channels[ch].read_state = AccessMode::FlipFlopLow;
                channels[ch].write_state = AccessMode::FlipFlopLow;
            } else {
                print("pit: Unhandled write to port {:#x}: {:#x}\n", port, value);
            }
        }

        uint32_t pio_read(uint16_t port, uint8_t size) {
            ASSERT(size == 1);

            if(port == ch0_data || port == ch1_data || port == ch2_data) {
                auto ch = port - ch0_data;

                auto count = get_channel_count(ch);

                using enum AccessMode;
                if(channels[ch].read_state == FlipFlopLow) {
                    channels[ch].read_state = FlipFlopHigh;
                    return count & 0xFF;
                } else if(channels[ch].read_state == FlipFlopHigh) {
                    channels[ch].read_state = FlipFlopLow;
                    return (count >> 8) & 0xFF;
                } else {
                    __builtin_unreachable();
                }
            } else if(port == channel2_status) {
                return (get_channel_output(2) << 5); // GRUB hack
            } else {
                print("pit: Unhandled read from port {:#x}\n", port);

                return 0;    
            }
        }

        private:
        struct Channel;

        void setup_channel(uint8_t ch) {
            auto irq_functor = [](void* userptr) {
                auto& self = *(vm::pit::Driver*)userptr;

                self.vm->cpus[0].thread->queue_apc([](void* vm) {
                    ((vm::Vm*)vm)->set_irq(irq_line, true);
                    ((vm::Vm*)vm)->set_irq(irq_line, false);
                }, self.vm);
                self.vm->cpus[0].thread->invoke_apcs();
            };
            
            switch (channels[ch].mode) {
                case 4: [[fallthrough]];
                case 0: { // Interrupt on Terminal Count
                    if(ch == 0) {
                        auto ns = ((uint64_t)channels[ch].count * 1'000'000'000) / clock_frequency;
                        //print("NS: {}\n", ns);

                        if(channels[ch].timer_idx.has_value())
                            ::hpet::cancel_timer(*channels[ch].timer_idx);

                        channels[ch].timer_idx = ::hpet::start_timer_ns(false, ns, irq_functor, this);
                        ASSERT(channels[ch].timer_idx.has_value());
                    }

                    channels[ch].start_tick = vm->cpus[0].get_guest_clock_ns();
                    break;
                }

                case 2: {
                    ASSERT(ch == 0);
                    auto ns = ((uint64_t)channels[ch].count * 1'000'000'000) / clock_frequency;
                    if(channels[ch].timer_idx.has_value())
                        ::hpet::cancel_timer(*channels[ch].timer_idx);

                    channels[ch].timer_idx = ::hpet::start_timer_ns(true, ns, irq_functor, this);
                    ASSERT(channels[ch].timer_idx.has_value());
                    break;
                }

                default:
                    PANIC("TODO: Unknown PIT mode");
            }
        }

        bool get_channel_output(uint8_t ch) {
            auto tick = get_tick(channels[ch]);

            switch (channels[ch].mode) {
                case 0: return tick >= channels[ch].count;
                case 4: return tick == channels[ch].count;
                default: PANIC("TODO");
            }
        }

        uint16_t get_channel_count(uint8_t ch) {
            ASSERT(ch == 2);

            switch (channels[ch].mode) {
                case 0: return (channels[ch].count - get_tick(channels[ch])) & 0xFFFF;
                default: PANIC("TODO");
            }
        }

        uint64_t get_tick(Channel& ch) {
            auto elapsed = vm->cpus[0].get_guest_clock_ns() - ch.start_tick;
            if(elapsed < 1000)
                return 0;
            else
                elapsed -= 1000;
            
            auto tick = (elapsed * clock_frequency) / 1'000'000'000;
            //print("Elapsed: {}, Tick: {}\n", elapsed, tick);
            return tick;
        }

        enum class AccessMode { FlipFlopLow, FlipFlopHigh };

        struct Channel {
            uint64_t start_tick;
            std::optional<uint32_t> timer_idx;
            uint32_t count;

            uint8_t mode;
            uint8_t count_latch;

            AccessMode write_state, read_state;
        } channels[3] = {};

        Vm* vm;
    };
} // namespace vm::pit
