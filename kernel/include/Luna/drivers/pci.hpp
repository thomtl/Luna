#pragma once

#include <Luna/common.hpp>
#include <Luna/mm/pmm.hpp>

#include <std/concepts.hpp>
#include <std/vector.hpp>

namespace pci {
    namespace msi {
        constexpr uint8_t id = 5;

        constexpr uint32_t control = 0x2;
        constexpr uint32_t addr = 0x4;
        constexpr uint32_t data_32 = 0x8;
        constexpr uint32_t data_64 = 0xC;

        union [[gnu::packed]] Control {
            struct {
                uint16_t enable : 1;
                uint16_t mmc : 1;
                uint16_t mme : 1;
                uint16_t c64 : 1;
                uint16_t pvm : 1;
                uint16_t reserved : 6;
            };
            uint16_t raw;
        };
        static_assert(sizeof(Control) == 2);

        union [[gnu::packed]] Address {
            struct {
                uint32_t reserved : 2;
                uint32_t destination_mode : 1;
                uint32_t redirection_hint : 1;
                uint32_t reserved_0 : 8;
                uint32_t destination_id : 8;
                uint32_t base_address : 12;
            };
            uint32_t raw;
        };
        static_assert(sizeof(Address) == 4);

        union [[gnu::packed]] Data {
            struct {
                uint32_t vector : 8;
                uint32_t delivery_mode : 3;
                uint32_t reserved : 3;
                uint32_t level : 1;
                uint32_t trigger_mode : 1;
                uint32_t reserved_0 : 16;
            };
            uint32_t raw;
        };
        static_assert(sizeof(Data) == 4);
    } // namespace msi

    namespace msix {
        constexpr uint8_t id = 0x11;
    } // namespace msix

    template<typename T>
    concept PciConfigValue = std::same_as<T, uint8_t> || std::same_as<T, uint16_t> || std::same_as<T, uint32_t>;

    struct Bar {
        enum class Type { Invalid, Pio, Mmio };
        Type type;
        uint64_t base;
        size_t len;
    };

    struct Device {
        uint16_t seg;
        uint8_t bus, slot, func;
        uintptr_t mmio_base;

        struct {
            bool supported = false;
            uint8_t offset = 0;
        } msi{};

        struct {
            bool supported = false;
            uint8_t offset = 0;
        } msix{};

        void enable_irq(uint8_t vector);
        Bar read_bar(size_t i) const;

        template<PciConfigValue T>
        T read(size_t offset) const {
            ASSERT(offset < pmm::block_size);
            return *(volatile T*)(mmio_base + offset);
        }

        template<PciConfigValue T>
        void write(size_t offset, T value) const {
            static_assert(std::same_as<T, uint8_t> || std::same_as<T, uint16_t> || std::same_as<T, uint32_t>);
            ASSERT(offset < pmm::block_size);
            *(volatile T*)(mmio_base + offset) = value;
        }
    };

    void init();
    Device& device_by_class(uint8_t class_code, uint8_t subclass_code, uint8_t prog_if, size_t i = 0);
    Device& device_by_id(uint16_t vid, uint16_t did, size_t i = 0);
    
    uint32_t read_raw(uint16_t seg, uint8_t bus, uint8_t slot, uint8_t func, size_t offset, size_t width);
    void write_raw(uint16_t seg, uint8_t bus, uint8_t slot, uint8_t func, size_t offset, uint32_t v, size_t width);

    template<PciConfigValue T>
    T read(uint16_t seg, uint8_t bus, uint8_t slot, uint8_t func, size_t offset) {
        return read_raw(seg, bus, slot, func, offset, sizeof(T));
    }

    template<PciConfigValue T>
    void write(uint16_t seg, uint8_t bus, uint8_t slot, uint8_t func, size_t offset, T value) {
        write_raw(seg, bus, slot, func, offset, value, sizeof(T));
    }
} // namespace pci