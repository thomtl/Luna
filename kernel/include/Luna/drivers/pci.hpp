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
            return *(T*)(mmio_base + offset);
        }

        template<PciConfigValue T>
        void write(size_t offset, T value) const {
            static_assert(std::same_as<T, uint8_t> || std::same_as<T, uint16_t> || std::same_as<T, uint32_t>);
            ASSERT(offset < pmm::block_size);
            *(T*)(mmio_base + offset) = value;
        }
    };

    void init();
    const std::vector<Device>& get_devices();


    template<PciConfigValue T>
    T read(uint16_t seg, uint8_t bus, uint8_t slot, uint8_t func, size_t offset) {
        ASSERT(offset < pmm::block_size);
        
        for(const auto& device : get_devices())
            if(device.seg == seg && device.bus == bus && device.slot == slot && device.func == func)
                return device.read<T>(offset);

        PANIC("Was not able to find Device");
    }

    template<PciConfigValue T>
    void write(uint16_t seg, uint8_t bus, uint8_t slot, uint8_t func, size_t offset, T value) {
        static_assert(std::same_as<T, uint8_t> || std::same_as<T, uint16_t> || std::same_as<T, uint32_t>);
        ASSERT(offset < pmm::block_size);
        
        for(const auto& device : get_devices())
            if(device.seg == seg && device.bus == bus && device.slot == slot && device.func == func)
                return device.write<T>(offset, value);

        PANIC("Was not able to find Device");
    }
} // namespace pci