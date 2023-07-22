#pragma once

#include <Luna/common.hpp>
#include <Luna/misc/uuid.hpp>
#include <Luna/fs/storage_dev.hpp>

namespace gpt {
    struct [[gnu::packed]] GPTHeader {
        char signature[8];
        uint32_t revision;
        uint32_t header_size;
        uint32_t header_crc32;
        uint32_t reserved_0;
        uint64_t my_lba;
        uint64_t alternate_lba;
        uint64_t first_usable_lba;
        uint64_t last_usable_lba;
        uint8_t disk_guid[16];
        uint64_t partition_entry_lba;
        uint32_t number_of_partition_entries;
        uint32_t size_of_partition_entry;
        uint32_t partition_entry_array_crc32;
    };
    static_assert(sizeof(GPTHeader) == 92);

    constexpr const char* header_sig = "EFI PART";
    constexpr uint32_t header_rev = (1 << 16) | (0 << 0); // 1.0

    struct [[gnu::packed]] GPTPartitionEntry {
        uint8_t type_guid[16];
        uint8_t unique_guid[16];
        uint64_t starting_lba;
        uint64_t ending_lba;
        uint64_t attributes;
        uint16_t partition_name[36];
    };

    


    void parse_gpt(storage_dev::Device& dev);
}