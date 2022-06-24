#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

#include <Luna/vmm/drivers/pci/pci.hpp>

#include <Luna/misc/log.hpp>

#include <std/unordered_map.hpp>


namespace vm::pci::ecam {
    struct EcamConfig {
        uintptr_t base, size;
        uint8_t bus_start, bus_end;
        bool enabled;
    };

    union [[gnu::packed]] EcamAddress {
        struct {
            uint64_t reg : 12;
            uint64_t func : 3;
            uint64_t slot : 5;
            uint64_t bus : 8;
            uint64_t reserved : 36;
        };
        uint64_t raw;
    };
    static_assert(sizeof(EcamAddress) == 8);

    struct Driver final : public vm::AbstractMMIODriver {
        Driver(Vm* vm, HostBridge* bridge, uint16_t segment): vm{vm}, bridge{bridge}, segment{segment}, curr_config{.base = 0, .size = 0, .bus_start = 0, .bus_end = 0, .enabled = false} {}

        void update_region(const EcamConfig& config) {
            if(curr_config.enabled)
                vm->mmio_map[curr_config.base] = {nullptr, 0}; // Deregister old region

            if(config.enabled)
                vm->mmio_map[config.base] = {this, config.size};

            curr_config = config;
        }

        void mmio_write(uintptr_t addr, uint64_t value, uint8_t size) {
            if(!curr_config.enabled)
                return;
            
            ASSERT(size == 1 || size == 2 || size == 4);
            const EcamAddress dev{.raw = addr};
            ASSERT(dev.bus >= curr_config.bus_start && dev.bus <= curr_config.bus_end);
            const auto did = did_from_ecam_addr(dev);

            if(bridge->drivers.contains(did.raw))
                bridge->drivers[did.raw]->pci_write(did, dev.reg, value, size);
            //else
            //    print("pci: Unhandled PCI write, dev: {}:{}.{}, reg: {:#x}, size: {}\n", did.bus, did.slot, did.func, dev.reg, (uint16_t)size);
        }

        uint64_t mmio_read(uintptr_t addr, uint8_t size) {
            if(!curr_config.enabled)
                return 0;
            
            ASSERT(size == 1 || size == 2 || size == 4);
            const EcamAddress dev{.raw = addr};
            ASSERT(dev.bus >= curr_config.bus_start && dev.bus <= curr_config.bus_end);
            const auto did = did_from_ecam_addr(dev);

            if(bridge->drivers.contains(did.raw))
                return bridge->drivers[did.raw]->pci_read(did, dev.reg, size);
            
            //print("pci: Unhandled PCI write, dev: {}:{}.{}, reg: {:#x}, size: {}\n", did.bus, did.slot, did.func, dev.reg, (uint16_t)size);
            return ~0;
        }

        private:
        constexpr DeviceID did_from_ecam_addr(const EcamAddress& addr) {
            DeviceID id{};
            id.seg = segment;
            id.bus = addr.bus;
            id.slot = addr.slot;
            id.func = addr.func;

            return id;
        }

        vm::Vm* vm;
        HostBridge* bridge;

        uint16_t segment;
        EcamConfig curr_config;
    };
} // namespace vm::pci
