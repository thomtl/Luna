#pragma once

#include <Luna/common.hpp>
#include <std/utility.hpp>

namespace ioapic
{
    namespace regs
    {
        constexpr uint32_t id = 0;
        constexpr uint32_t version = 1;
        constexpr uint32_t arbitration = 2;
        constexpr uint32_t entry(size_t n) { return 0x10 + (n * 2); }

        enum class DeliveryMode : uint8_t {
            Fixed = 0b000,
            LowPriority = 0b001,
            Smi = 0b010,
            Nmi = 0b100,
            Init = 0b101,
            ExtInt = 0b111
        };

        enum class DestinationMode : uint8_t {
            Physical = 0,
            Logical = 1
        };
    } // namespace regs
    

    class Ioapic {
        public:
        Ioapic(uintptr_t pa, uint32_t gsi_base);

        std::pair<uint32_t, uint32_t> gsi_range() const { return {gsi_base, gsi_base + n_redirection_entries}; }

        void set(uint8_t i, uint8_t vector, regs::DeliveryMode delivery, regs::DestinationMode dest, uint16_t flags, uint32_t lapic_id);
        void mask(uint8_t i);
        void unmask(uint8_t i);

        private:
        uint32_t read(uint32_t i);
        void write(uint32_t i, uint32_t v);

        void set_entry(uint32_t i, uint64_t v);
        uint64_t read_entry(uint32_t i);

        uint32_t gsi_base;
        uintptr_t mmio_base;
        size_t n_redirection_entries;
    };

    void init();
    void set(uint32_t gsi, uint8_t vector, regs::DeliveryMode delivery, regs::DestinationMode dest, uint16_t flags, uint32_t lapic_id);
    void mask(uint32_t gsi);
    void unmask(uint32_t gsi);
} // namespace ioapic
