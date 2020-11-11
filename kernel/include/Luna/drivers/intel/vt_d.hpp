#pragma once

#include <Luna/common.hpp>

#include <Luna/drivers/acpi.hpp>
#include <Luna/drivers/pci.hpp>

#include <Luna/drivers/intel/sl_paging.hpp>

#include <std/bitmap.hpp>
#include <std/unordered_map.hpp>

namespace vt_d {
    struct [[gnu::packed]] Dmar {
        static constexpr const char* signature = "DMAR";
        acpi::SDTHeader header;
        uint8_t host_address_width;
        union {
            struct {
                uint8_t irq_remap : 1;
                uint8_t x2APIC_opt_out : 1;
                uint8_t dma_control_opt_in : 1;
                uint8_t reserved : 5;
            };
            uint8_t raw;
        } flags;
        uint8_t reserved_0[10];
    };
    static_assert(acpi::Table<Dmar>);

    struct [[gnu::packed]] Drhd {
        static constexpr uint16_t id = 0;
        uint16_t type;
        uint16_t length;
        struct {
            uint8_t pci_include_all : 1;
            uint8_t reserved : 7;
        } flags;
        uint8_t reserved_0;
        uint16_t segment;
        uint64_t mmio_base;
        uint8_t device_scope[];
    };

    struct [[gnu::packed]] RootTable {
        struct [[gnu::packed]] {
            uint64_t present : 1;
            uint64_t reserved : 11;
            uint64_t context_table : 52;
            uint64_t reserved_0;
        } entries[256];

        auto& operator[](size_t i) {
            return entries[i];
        }
    };
    static_assert(sizeof(RootTable) == pmm::block_size);

    struct [[gnu::packed]] ContextTable {
        struct [[gnu::packed]] {
            uint64_t present : 1;
            uint64_t fault_processing_disable : 1;
            uint64_t translation_type : 2;
            uint64_t reserved : 8;
            uint64_t sl_translation_ptr : 52;
            uint64_t address_width : 3;
            uint64_t ignored : 4;
            uint64_t reserved_0 : 1;
            uint64_t domain_id : 16;
            uint64_t reserved_1 : 40;
        } entries[256];

        auto& operator[](size_t i) {
            return entries[i];
        }
    };
    static_assert(sizeof(ContextTable) == pmm::block_size);

    struct [[gnu::packed]] FaultRecordingRegister {
        uint64_t reserved : 12;
        uint64_t fault_info : 52;
        uint64_t source_id : 16;
        uint64_t reserved_0 : 12;
        uint64_t type_bit_2 : 1;
        uint64_t supervisor : 1;
        uint64_t execute : 1;
        uint64_t pasid_present : 1;
        uint64_t reason : 8;
        uint64_t pasid : 20;
        uint64_t address_type : 2;
        uint64_t type_bit_1 : 1;
        uint64_t fault : 1;
    };
    static_assert(sizeof(FaultRecordingRegister) == 16);

    struct [[gnu::packed]] IOTLB {
        uint64_t addr;
        uint64_t cmd;
    };
    static_assert(sizeof(IOTLB) == 16);

    union [[gnu::packed]] IOTLBCmd {
        struct {
            uint64_t reserved : 32;
            uint64_t domain_id : 16;
            uint64_t drain_writes : 1;
            uint64_t drain_reads : 1;
            uint64_t reserved_0 : 7;
            uint64_t invl_granularity : 2;
            uint64_t reserved_1 : 1;
            uint64_t req_granularity : 2;
            uint64_t reserved_2 : 1;
            uint64_t invalidate : 1;
        };
        uint64_t raw;
    };
    static_assert(sizeof(IOTLBCmd) == 8);

    union [[gnu::packed]] IOTLBAddr {
        struct {
            uint64_t mask : 6;
            uint64_t hint : 1;
            uint64_t reserved : 5;
            uint64_t addr : 52;
        };
        uint64_t raw;
    };
    static_assert(sizeof(IOTLBAddr) == 8);

    struct [[gnu::packed]] RemappingEngineRegs {
        uint32_t version;
        uint32_t reserved;
        uint64_t capabilities;
        uint64_t extended_capabilities;
        uint32_t global_command;
        uint32_t global_status;

        union [[gnu::packed]] RootTableAddress {
            struct {
                uint64_t reserved : 10;
                uint64_t translation_type : 2;
                uint64_t address : 52;
            };
            uint64_t raw;
        };
        uint64_t root_table_address;
        uint64_t context_command;
        uint32_t reserved_0;
        uint32_t fault_status;
        uint32_t fault_event_control;
        uint32_t fault_event_data;
        uint32_t fault_event_address;

        union [[gnu::packed]] FaultEventUpperAddress {
            struct {
                uint32_t reserved : 8;
                uint32_t upper_dest_id : 24;
            };
            uint32_t raw;
        };
        uint32_t fault_event_upper_address;

        uint64_t reserved_1[2];
        uint64_t advanced_fault_log;
        uint32_t reserved_2;
        uint32_t protected_mem_enable;
        uint32_t protected_low_mem_base;
        uint32_t protected_low_mem_limit;
        uint64_t protected_high_mem_base;
        uint64_t protected_high_mem_limit;
        uint64_t invalidation_queue_head;
        uint64_t invalidation_queue_tail;
        uint64_t invalidation_queue_address;
        uint32_t reserved_3;
        uint32_t invalidation_completion_status;
        uint32_t invalidation_completion_event_control;
        uint32_t invalidation_completion_event_data;
        uint32_t invalidation_completion_event_address;
        uint32_t invalidation_completion_event_upper_address;
        uint64_t invalidation_queue_event_record;
        uint64_t irq_remapping_table_base;
        uint64_t page_request_queue_head;
        uint64_t page_request_queue_tail;
        uint64_t page_request_queue_address;
        uint32_t reserved_4;
        uint32_t page_request_status;
        uint32_t page_request_event_control;
        uint32_t page_request_event_data;
        uint32_t page_request_event_address;
        uint32_t page_request_event_upper_address;

        uint64_t mtrr_cap;
        uint64_t default_mtrr;
        uint64_t mtrrs[11];

        struct {
            uint64_t base;
            uint64_t mask;
        } variable_mask[10];

        uint64_t virtual_command_capability;
        uint64_t reserved_5;
        uint64_t virtual_command;
        uint64_t reserved_6;
        uint64_t virtual_command_respond;
        uint64_t reserved_7;
    };

    union [[gnu::packed]] SourceID {
        struct {
            uint16_t func : 3;
            uint16_t slot : 5;
            uint16_t bus : 8;
        };
        uint16_t raw;

        static constexpr SourceID from_device(const pci::Device& device) {
            SourceID id{};
            id.bus = device.bus;
            id.slot = device.slot;
            id.func = device.func;

            return id;
        }
    };
    static_assert(sizeof(SourceID) == 2);


    class RemappingEngine {
        public:
        RemappingEngine(Drhd* drhd);
        sl_paging::context& get_device_translation(SourceID device);

        size_t n_fault_recording_regs;
        size_t segment;

        void invalidate_iotlb_addr(SourceID device, uintptr_t iova);

        private:

        void wbflush();
        void invalidate_global_context();
        void invalidate_iotlb();

        Drhd* drhd;

        volatile RemappingEngineRegs* regs;
        volatile FaultRecordingRegister* fault_recording_regs;
        volatile IOTLB* iotlb_regs;

        volatile RootTable* root_table;

        uint8_t secondary_page_levels;
        size_t n_domain_ids;
        std::bitmap domain_ids;
        std::unordered_map<uint16_t, sl_paging::context*> page_map;
        std::unordered_map<uint16_t, uint16_t> domain_id_map;

        bool x2apic_mode, wbflush_needed;
    };

    struct IOMMU {
        IOMMU();

        
        void map(const pci::Device& device, uintptr_t pa, uintptr_t iova, uint64_t flags);
        uintptr_t unmap(const pci::Device& device, uintptr_t iova);
        void invalidate_iotlb_entry(const pci::Device& device, uintptr_t iova);

        private:
        RemappingEngine& get_engine(const pci::Device& device);

        Dmar* dmar;
        std::vector<RemappingEngine> engines;
    };
} // namespace vt_d