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
                uint16_t mmc : 3;
                uint16_t mme : 3;
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
                uint32_t base_address : 12; // Must be 0xFEE
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

        constexpr uint16_t control = 0x2;
        constexpr uint16_t table = 0x4;
        constexpr uint16_t pending = 0x8;

        union [[gnu::packed]] Control {
            struct {
                uint16_t n_irqs : 11;
                uint16_t reserved : 3;
                uint16_t mask : 1;
                uint16_t enable : 1;
            };
            uint16_t raw;
        };

        union [[gnu::packed]] Addr {
            struct {
                uint32_t bir : 3;
                uint32_t offset : 29;
            };
            uint32_t raw;
        };


        union [[gnu::packed]] VectorControl {
            struct {
                uint32_t mask : 1;
                uint32_t reserved : 31;
            };
            uint32_t raw;
        };

        struct [[gnu::packed]] Entry {
            uint32_t addr_low;
            uint32_t addr_high;
            uint32_t data;
            uint32_t control;
        };
    } // namespace msix

    namespace power {
        constexpr uint8_t id = 1;

        constexpr uint16_t cap = 0x2;
        constexpr uint16_t control = 0x4;

        union [[gnu::packed]] Cap {
            struct {
                uint16_t version : 3;
                uint16_t pme_clock : 1;
                uint16_t reserved : 1;
                uint16_t device_specific_init : 1;
                uint16_t aux_current : 3;
                uint16_t d1_support : 1;
                uint16_t d2_support : 1;
                uint16_t pme_support : 5;
            };
            uint16_t raw;
        };

        union [[gnu::packed]] Control {
            struct {
                uint16_t power_state : 2;
                uint16_t reserved : 6;
                uint16_t pme_enable : 1;
                uint16_t data_select : 4;
                uint16_t data_scale : 2;
                uint16_t pme_status : 1;
            };
            uint16_t raw;
        };
    } // namespace power
    
    namespace pcie {
        constexpr uint8_t id = 0x10;
    } // namespace pcie
    

    template<typename T>
    concept PciConfigValue = std::same_as<T, uint8_t> || std::same_as<T, uint16_t> || std::same_as<T, uint32_t>;

    struct Bar {
        enum class Type { Invalid, Pio, Mmio };
        Type type;
        uint64_t base;
        size_t len;
    };

    namespace privileges {
        enum : uint8_t {
            Pio = (1 << 0), 
            Mmio = (1 << 1),
            Dma = (1 << 2),
        };    
    } // namespace privileges

    enum class BridgeType {
        None, PCI_to_PCIe, PCIe_to_PCIe
    };

    union [[gnu::packed]] RequesterID {
        struct {
            uint16_t func : 3;
            uint16_t slot : 5;
            uint16_t bus : 8;
        };
        uint16_t raw;
    };
        

    struct Device {
        uint16_t seg;
        uint8_t bus, slot, func;
        uintptr_t mmio_base;

        BridgeType bridge_type = BridgeType::None;
        RequesterID requester_id;

        struct {
            bool supported = false;
            uint8_t offset = 0;

            uint8_t supported_states = 0;
            uint8_t state = 0;
        } power{};

        struct {
            bool supported = false;
            uint8_t offset = 0;
        } msi{};

        struct {
            bool supported = false;
            uint8_t offset = 0;

            uint16_t n_messages;
            struct {
                uint8_t bar;
                uint32_t offset;
            } table, pending;
        } msix{};

        struct {
            bool found = false;
            uint8_t offset = 0;
        } pcie{};

        void enable_irq(uint16_t i, uint8_t vector);
        bool set_power(uint8_t state);

        Bar read_bar(size_t i) const;
        void set_privileges(uint8_t privilege);

        template<PciConfigValue T>
        T read(size_t offset) const {
            ASSERT(offset < pmm::block_size);
            return *(volatile T*)(mmio_base + offset);
        }

        template<PciConfigValue T>
        void write(size_t offset, T value) const {
            ASSERT(offset < pmm::block_size);
            *(volatile T*)(mmio_base + offset) = value;
        }
    };

    void init();
    Device* device_by_class(uint8_t class_code, uint8_t subclass_code, uint8_t prog_if, size_t i = 0);
    Device* device_by_id(uint16_t vid, uint16_t did, size_t i = 0);
    Device* device_by_location(uint16_t seg, uint8_t bus, uint8_t slot, uint8_t func);
    
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

    namespace match {
        constexpr uint32_t class_code = (1 << 0);
        constexpr uint32_t subclass_code = (1 << 1);
        constexpr uint32_t protocol_code = (1 << 2);
        constexpr uint32_t vendor_device = (1 << 3);
    } // namespace match

    struct Driver {
        const char* name;

        void (*init)(Device& device);

        uint32_t match;

        uint8_t class_code = 0, subclass_code = 0, protocol_code = 0;

        std::span<std::pair<uint16_t, uint16_t>> id_list = {};
    };

    #define DECLARE_PCI_DRIVER(driver) [[maybe_unused, gnu::used, gnu::section(".pci_drivers")]] static pci::Driver* pci_driver_##driver = &driver

    void init_drivers();
} // namespace pci