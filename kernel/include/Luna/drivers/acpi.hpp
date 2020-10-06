#pragma once

#include <Luna/common.hpp>
#include <Luna/misc/stivale2.hpp>

#include <std/concepts.hpp>

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

    void init(const stivale2::Parser& parser);
    SDTHeader* get_table(const char* sig, size_t index);
    
    template<Table T>
    T* get_table(size_t index = 0) {
        return (T*)get_table(T::signature, index);
    }
} // namespace acpi
