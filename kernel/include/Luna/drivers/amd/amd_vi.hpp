#pragma once

#include <Luna/common.hpp>

#include <Luna/drivers/acpi.hpp>
#include <Luna/drivers/pci.hpp>

#include <Luna/drivers/amd/io_paging.hpp>

#include <std/vector.hpp>
#include <std/bitmap.hpp>
#include <std/unordered_map.hpp>

#include <std/concepts.hpp>

namespace amd_vi {
    struct [[gnu::packed]] Ivrs {
        static constexpr const char* signature = "IVRS";
        acpi::SDTHeader header;
        
        union [[gnu::packed]] IvInfo {
            struct {
                uint32_t extended_feature : 1;
                uint32_t preboot_dma : 1;
                uint32_t reserved : 3;
                uint32_t guest_va_width : 3;
                uint32_t pa_width : 7;
                uint32_t va_width : 7;
                uint32_t ats_reserved : 1;
                uint32_t reserved_0 : 9;
            };
            uint32_t raw;
        };
        uint32_t iv_info;
        uint64_t reserved;
        uint8_t ivdb[];
    };
    static_assert(acpi::Table<Ivrs>);

    struct [[gnu::packed]] Type10IVHD {
        static constexpr uint8_t sig = 0x10;
        uint8_t type;
        uint8_t flags;
        uint16_t length;
        uint16_t device_id;
        uint16_t capability_offset;
        uint64_t iommu_base;
        uint16_t pci_segment;
        uint16_t iommu_info;
        uint32_t iommu_features;
        uint8_t ivhd_devices[];
    };

    struct [[gnu::packed]] IVHDEntry {
        uint8_t type;
        uint16_t device_id;
        uint8_t flags;
        uint32_t ext;
        uint32_t hidh;
        uint64_t cid;
        uint8_t uidf;
        uint8_t uidl;
        uint8_t uid;
    };

    union [[gnu::packed]] DeviceID {
        struct {
            uint16_t func : 3;
            uint16_t slot : 5;
            uint16_t bus : 8;
        };
        uint16_t raw;

        static constexpr DeviceID from_device(const pci::Device& device) {
            DeviceID id{};
            id.bus = device.bus;
            id.slot = device.slot;
            id.func = device.func;

            return id;
        }

        constexpr bool operator<=(const DeviceID& other) const {
            return raw <= other.raw;
        }

        constexpr bool operator>=(const DeviceID& other) const {
            return raw >= other.raw;
        }
    };
    static_assert(sizeof(DeviceID) == 2);

    struct [[gnu::packed]] IOMMUEngineRegs {
        uint64_t device_table_base;
        uint64_t command_buffer_base;
        uint64_t event_log_base;
        uint64_t control;
        uint64_t exclusion_base;
        uint64_t exclusion_limit;
        uint64_t extended_features;
        struct [[gnu::packed]] {
            uint64_t ppr_log_base;
            uint64_t hw_event_upper_reg;
            uint64_t hw_event_lower_reg;
            uint64_t hw_event_status_reg;
        } ppr;
        uint64_t reserved;
        struct [[gnu::packed]] {
            uint64_t filters[16];
        } smi_filter;
        struct [[gnu::packed]] {
            uint64_t log_base;
            uint64_t log_tail;
        } vapic;
        struct [[gnu::packed]] {
            uint64_t ppr_log_b_base;
            uint64_t event_log_b_base;
        } alt_ppr;
        struct [[gnu::packed]] {
            uint64_t base[7];
        } device_table_segment;
        struct [[gnu::packed]] {
            uint64_t feature_extension;
            uint64_t control_extension;
            uint64_t status_extension;
        } features;
        struct [[gnu::packed]] {
            uint32_t vector0;
            uint32_t vector1;
            uint32_t capability;
            uint32_t address_low;
            uint32_t address_heigh;
            uint32_t data;
            uint32_t mapping_capability;
        } msi;
        struct [[gnu::packed]] {
            uint32_t control;
        } perf_opt;
        struct [[gnu::packed]] {
            uint64_t general_irq_control;
            uint64_t ppr_interrupt_control;
            uint64_t ga_log_interrupt_control;
        } xt;
        uint64_t reserved_0[15];
        struct [[gnu::packed]] {
            struct {
                uint64_t base;
                uint64_t relocation;
                uint64_t length;
            } aperture[4];
        } marc;
        uint64_t reserved_1[0x3B3];
        uint64_t reserved_register;
        struct [[gnu::packed]] {
            uint64_t cmd_buf_head;
            uint64_t cmd_buf_tail;
            uint64_t event_log_head;
            uint64_t event_log_tail;
        } cmd_evt_ptrs;
        uint64_t iommu_status;
        uint64_t reserved_2;
        struct [[gnu::packed]] {
            uint64_t head;
            uint64_t tail;
        } ppr_log_ptrs;
        struct [[gnu::packed]] {
            uint64_t head;
            uint64_t tail;
        } vapic_log_ptrs;
        struct [[gnu::packed]] {
            uint64_t head;
            uint64_t tail;
        } ppr_log_b_ptrs;
        uint64_t reserved_3[2];
        struct [[gnu::packed]] {
            uint64_t head;
            uint64_t tail;
        } event_log_b_ptrs;
        struct [[gnu::packed]] {
            uint64_t ppr_log_auto_response;
            uint64_t ppr_log_overflow_early_indicator;
            uint64_t ppr_log_b_overflow_early_indicator;
        } ppr_log_overflow_protection;
    };

    struct [[gnu::packed]] DeviceTableEntry {
        uint64_t valid : 1;
        uint64_t translation_info_valid : 1;
        uint64_t reserved : 5;
        uint64_t dirty_control : 2;
        uint64_t paging_mode : 3;
        uint64_t page_table_root_ptr : 40;
        uint64_t ppr_enable : 1;
        uint64_t gprp_enable : 1;
        uint64_t guest_io_protection_valid : 1;
        uint64_t guest_translation_valid : 1;
        uint64_t guest_levels_translated : 2;
        uint64_t guest_cr3_table_root_pointer_low : 3;
        uint64_t io_read_permission : 1;
        uint64_t io_write_permission : 1;
        uint64_t reserved_0 : 1;
        uint64_t domain_id : 16;
        uint64_t guest_cr3_table_root_pointer_middle : 16;
        uint64_t iotlb_enable : 1;
        uint64_t suppress_io_page_faults : 1;
        uint64_t supress_all_io_page_faults : 1;
        uint64_t port_io_control : 2;
        uint64_t iotlb_cache_hint : 1;
        uint64_t snoop_disable : 1;
        uint64_t allow_exclusion : 1;
        uint64_t system_managment_enable : 2;
        uint64_t reserved_1 : 1;
        uint64_t guest_cr3_table_root_pointer_high : 21;
        uint64_t interrupt_map_valid : 1;
        uint64_t interrupt_table_length : 4;
        uint64_t ignore_unmapped_interrupts : 1;
        uint64_t interrupt_table_root_ptr : 46;
        uint64_t reserved_2 : 4;
        uint64_t init_pass : 1;
        uint64_t einit_pass : 1;
        uint64_t nmi_pass : 1;
        uint64_t reserved_3 : 1;
        uint64_t interrupt_control : 2;
        uint64_t lint0_pass : 1;
        uint64_t lint1_pass : 1;
        uint64_t reserved_4 : 32;
        uint64_t reserved_5 : 22;
        uint64_t attribute_override : 1;
        uint64_t mode0fc : 1;
        uint64_t snoop_attribute : 8;
    };
    static_assert(sizeof(DeviceTableEntry) == (256 / 8));

    template<typename T>
    concept IOMMUCommand = requires(T t) {
        { T::opcode } -> std::convertible_to<uint8_t>;
        { t.op } -> std::convertible_to<uint8_t>;
    } && (sizeof(T) == 16);

    struct [[gnu::packed]] CmdCompletionWait {
        static constexpr uint8_t opcode = 0x1;
        uint64_t store : 1;
        uint64_t irq : 1;
        uint64_t flush : 1;
        uint64_t store_address : 49;
        uint64_t reserved : 8;
        uint64_t op : 4;
        uint64_t store_data;
    };
    static_assert(IOMMUCommand<CmdCompletionWait>);

    struct [[gnu::packed]] CmdInvalidateDevTabEntry {
        static constexpr uint8_t opcode = 0x2;
        uint32_t device_id : 16;
        uint32_t reserved : 16;
        uint32_t reserved_0 : 28;
        uint32_t op : 4;
        uint32_t reserved_1;
        uint32_t reserved_2;
    };
    static_assert(IOMMUCommand<CmdInvalidateDevTabEntry>);

    struct [[gnu::packed]] CmdInvalidateIOMMUPages {
        static constexpr uint8_t opcode = 0x3;
        uint32_t pasid : 20;
        uint32_t reserved : 12;
        uint32_t domain_id : 16;
        uint32_t reserved_0 : 12;
        uint64_t op : 4;
        uint64_t s : 1;
        uint64_t pde : 1;
        uint64_t gn : 1;
        uint64_t reserved_1 : 9;
        uint64_t address : 52;
    };
    static_assert(IOMMUCommand<CmdInvalidateIOMMUPages>);

    struct [[gnu::packed]] CmdInvalidateIOMMUAll {
        static constexpr uint8_t opcode = 0x8;
        uint32_t reserved;
        uint32_t reserved_0 : 28;
        uint32_t op : 4;
        uint32_t reserved_1;
        uint32_t reserved_2;
    };
    static_assert(IOMMUCommand<CmdInvalidateIOMMUAll>);

    enum class EngineControl : uint64_t {
        IOMMUEnable = (1ull << 0),
        HyperTransportTunnelEnable = (1ull << 1),
        EventLogEnable = (1ull << 2),
        EventIRQEnable = (1ull << 3),
        CompletionWaitIRQEnable = (1ull << 4),
        PassPostedWrite = (1ull << 8),
        ResponsePassPostedWrite = (1ull << 9),
        Coherent = (1ull << 10),
        Isochronous = (1ull << 11),
        CommandBufferEnable = (1ull << 12),
        PPRLogEnable = (1ull << 13),
        PPRIRQEnable = (1ull << 14),
        PPREnable = (1ull << 15),
        GuestTranslationEnable = (1ull << 16),
        GuestVAPICEnable = (1ull << 17),
        SMIFilterEnable = (1ull << 22),
        SelfWBDisable = (1ull << 23),
        SMIFilterLogEnable = (1ull << 24),
        GuestVAPICGALogEnable = (1ull << 28),
        GuestVAPICIRQEnable = (1ull << 29),
        PPRAutoResponseEnable = (1ull << 29),
        MARCEnable = (1ull << 40),
        BlockStopMarkEnable = (1ull << 41),
        PPRAutoResponse = (1ull << 42),
        EnhancedPPRHandling = (1ull << 45),
        DirtyUpdateDisable = (1ull << 48),
        XTEnable = (1ull << 50),
        XTIRQGen = (1ull << 51),
        AccessedUpdateDisable = (1ull << 54),

        ControlInvalidateTimeout = 5,
        ControlInvalidateTimeoutMask = (0b111 < ControlInvalidateTimeout)
    };

    enum class IVHDEntryTypes : uint8_t {
        DeviceAll = 0x1,
        DeviceSelect = 0x2,
        DeviceSelectRangeStart = 0x2,
        DeviceSelectRangeEnd = 0x3,
        DeviceRangeEnd = 0x4,
        DeviceAlias = 0x42,
        DeviceAliasRange = 0x43,
        DeviceExtSelect = 0x46,
        DeviceExtSelectRange = 0x37,
        DeviceSpecial = 0x48,
        DeviceACPIHID = 0xF0,
    };

    constexpr size_t n_domains = 65536; // AMD does not have a limit to it like intel?

    struct IOMMUEngine {
        IOMMUEngine(Type10IVHD* ivhd);

        io_paging::context& get_translation(const DeviceID& device);
        void invalidate_iotlb_addr(const DeviceID& device, uintptr_t iova);

        uint16_t segment;
        DeviceID start, end;

        private:
        void disable();
        void init_flags();
        void erratum_746_workaround();

        uint16_t get_highest_device_id();

        void flush_all_caches();
        
        void cmd_invalidate_devtab_entry(const DeviceID& device);
        void cmd_invalidate_all();

        template<IOMMUCommand T>
        bool queue_command(const T& cmd) {
            return queue_command((const uint8_t*)&cmd, sizeof(cmd));
        }

        bool queue_command(const uint8_t* cmd, size_t size);
        void completion_wait();

        volatile IOMMUEngineRegs* regs;
        volatile DeviceTableEntry* device_table;

        uint8_t page_levels;
        uint16_t max_device_id;
        std::bitmap domain_ids;

        std::unordered_map<uint16_t, io_paging::context*> page_map;
        std::unordered_map<uint16_t, uint16_t> domain_id_map;

        struct {
            volatile uint8_t* ring;
            uint64_t head, tail;
            size_t length;

            bool need_sync;
        } cmd_ring, evt_ring;

        volatile uint64_t* cmd_sem;
        uintptr_t cmd_sem_pa;

        Type10IVHD* ivhd;
        pci::Device* pci_dev;
    };

    struct IOMMU {
        IOMMU();
        
        void map(const pci::Device& device, uintptr_t pa, uintptr_t iova, uint64_t flags);
        uintptr_t unmap(const pci::Device& device, uintptr_t iova);
        void invalidate_iotlb_entry(const pci::Device& device, uintptr_t iova);
        private:
        IOMMUEngine& engine_for_device(uint16_t seg, const DeviceID& id);

        Ivrs* ivrs;

        std::vector<IOMMUEngine> engines;
    };

    bool has_iommu();
} // namespace amd_vi
