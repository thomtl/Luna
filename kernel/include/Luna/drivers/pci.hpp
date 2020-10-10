#pragma once

#include <Luna/common.hpp>
#include <Luna/mm/pmm.hpp>

#include <std/concepts.hpp>
#include <std/vector.hpp>

namespace pci {
    template<typename T>
    concept PciConfigValue = std::same_as<T, uint8_t> || std::same_as<T, uint16_t> || std::same_as<T, uint32_t>;

    struct Device {
        uint16_t seg;
        uint8_t bus, slot, func;
        uintptr_t mmio_base;

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