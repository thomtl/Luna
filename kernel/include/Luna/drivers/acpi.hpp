#pragma once

#include <Luna/common.hpp>
#include <Luna/misc/stivale2.hpp>

#include <std/concepts.hpp>
#include <std/vector.hpp>
#include <std/span.hpp>

#include <lai/core.h>

namespace acpi {
    struct [[gnu::packed]] Rsdp {
        const char sig[8];
        uint8_t checksum;
        const char oemid[6];
        uint8_t revision;
        uint32_t rsdt;
    };

    struct [[gnu::packed]] Xsdp {
        Rsdp rsdp;
        uint32_t length;
        uint64_t xsdt;
        uint8_t ext_checksum;
        uint8_t reserved[3];
    };

    struct [[gnu::packed]] SDTHeader {
        const char sig[4];
        uint32_t length;
        uint8_t revision;
        uint8_t checksum;
        const char oemid[6];
        const char oem_table_id[8];
        uint32_t oem_revision;
        uint32_t creator_id;
        uint32_t creator_revision;
    };

    template<typename T>
    concept Table = requires (T t) {
        { T::signature } -> std::convertible_to<const char*>;
        { t.header } -> std::convertible_to<SDTHeader>;
    };

    struct [[gnu::packed]] Rsdt {
        SDTHeader header;
        uint32_t sdts[];
    };

    struct [[gnu::packed]] Xsdt {
        SDTHeader header;
        uint64_t sdts[];
    };

    struct [[gnu::packed]] GenericAddressStructure {
        uint8_t id;
        uint8_t width;
        uint8_t offset;
        uint8_t access_size;
        uint64_t address;
    };

    struct [[gnu::packed]] Fadt {
        static constexpr const char* signature = "FACP";
        SDTHeader header;
        uint32_t firmware_ctrl;
        uint32_t dsdt;
        uint8_t reserved;
        uint8_t preferred_pm_profile;
        uint16_t sci_int;
        uint32_t sci_cmd;
        uint8_t acpi_enable;
        uint8_t acpi_disable;
        uint8_t s4bios_req;
        uint8_t pstate_cnt;
        uint32_t pm1a_evt_blk;
        uint32_t pm1b_evt_blk;
        uint32_t pm1a_cnt_blk;
        uint32_t pm1b_cnt_blk;
        uint32_t pm2_cnt_blk;
        uint32_t pm_tmr_blk;
        uint32_t gpe0_blk;
        uint32_t gpe1_blk;
        uint8_t pm1_evt_len;
        uint8_t pm1_cnt_len;
        uint8_t pm2_cnt_len;
        uint8_t pm_tmr_len;
        uint8_t gpe0_blk_len;
        uint8_t gpe1_blk_len;
        uint8_t gpe1_base;
        uint8_t cst_cnt;
        uint16_t p_lvl2_lat;
        uint16_t p_lvl3_lat;
        uint16_t flush_size;
        uint16_t flush_stride;
        uint8_t duty_offset;
        uint8_t duty_width;
        uint8_t day_alarm;
        uint8_t month_alarm;
        uint8_t century;
        uint16_t iapc_boot_arch;
        uint8_t reserved0;
        uint32_t flags;
        GenericAddressStructure reset_reg;
        uint8_t reset_value;
        uint16_t arm_boot_arch;
        uint8_t minor_version;
        uint64_t x_firmware_ctrl;
        uint64_t x_dsdt;
        GenericAddressStructure x_pm1a_evt_blk;
        GenericAddressStructure x_pm1b_evt_blk;
        GenericAddressStructure x_pm1a_cnt_blk;
        GenericAddressStructure x_pm1b_cnt_blk;
        GenericAddressStructure x_pm2_cnt_blk;
        GenericAddressStructure x_pm_tmr_blk;
        GenericAddressStructure x_gpe0_blk;
        GenericAddressStructure x_gpe1_blk;
        GenericAddressStructure sleep_control_reg;
        GenericAddressStructure sleep_status_reg;
        uint64_t hypervisor_id;
    };
    static_assert(Table<Fadt>);

    struct [[gnu::packed]] Mcfg {
        static constexpr const char* signature = "MCFG";
        SDTHeader header;
        uint64_t reserved;
        struct [[gnu::packed]] Allocation {
            uint64_t base;
            uint16_t segment;
            uint8_t start_bus;
            uint8_t end_bus;
            uint32_t reserved;
        } allocations[];
    };
    static_assert(Table<Mcfg>);

    struct [[gnu::packed]] Hpet {
        static constexpr const char* signature = "HPET";
        SDTHeader header;
        uint32_t block_id;
        GenericAddressStructure base;
        uint8_t uid;
        uint16_t minimum_tick;
        uint8_t protection;
    };
    static_assert(Table<Hpet>);

    struct [[gnu::packed]] Madt {
        static constexpr const char* signature = "APIC";
        SDTHeader header;
        uint32_t lapic_addr;
        uint32_t flags;
    };
    static_assert(Table<Madt>);
    
    

    void init(const stivale2::Parser& parser);
    void init_sci();
    SDTHeader* get_table(const char* sig, size_t index);

    bool eval_osc(lai_nsnode_t* node, bool query, uint32_t revision, const uint8_t uuid[16], std::span<uint32_t>& buffer);
    
    template<Table T>
    T* get_table(size_t index = 0) {
        return (T*)get_table(T::signature, index);
    }

    struct [[gnu::packed]] MadtEntryHeader {
        uint8_t type;
        uint8_t length;
    };

    template<typename T>
    concept MadtEntry = requires(T t) {
        { T::type } -> std::convertible_to<uint8_t>;
        { t.header } -> std::convertible_to<MadtEntryHeader>;
    };

    struct [[gnu::packed]] IoapicMadtEntry {
        static constexpr uint8_t type = 1;
        MadtEntryHeader header;
        uint8_t id;
        uint8_t reserved;
        uint32_t addr;
        uint32_t gsi_base;
    };
    
    struct [[gnu::packed]] ISOMadtEntry {
        static constexpr uint8_t type = 2;
        MadtEntryHeader header;
        uint8_t bus;
        uint8_t src;
        uint32_t gsi;
        uint16_t flags;
    };

    struct MadtParser {
        MadtParser() {
            madt = get_table<Madt>();
            ASSERT(madt);
        }

        bool has_legacy_pic() {
            return madt->flags & 1;
        }

        template<MadtEntry T>
        std::vector<T> get_entries_of_type() {
            std::vector<T> ret{};

            auto entries_size = (madt->header.length - sizeof(Madt));
            auto entries = (uintptr_t)madt + sizeof(Madt);
            uint64_t offset = 0;
            while(offset < entries_size) {
                auto* item = (MadtEntryHeader*)(entries + offset);
                if(item->type == T::type)
                    ret.push_back(*(T*)(item));

                offset += item->length;
            }

            return ret;
        }

        private:
        Madt* madt;
    };
} // namespace acpi
