#pragma once

#include <Luna/common.hpp>
#include <Luna/fs/storage_dev.hpp>

namespace mbr {
    struct [[gnu::packed]] PartitionTable {
        struct [[gnu::packed]] Entry {
            uint8_t attributes;
            uint8_t chs_start[3];
            uint8_t type;
            uint8_t chs_end[3];
            uint32_t lba_start;
            uint32_t n_sectors;
        };
        static_assert(sizeof(Entry) == 16);

        Entry entries[4];
        uint16_t magic;
    };

    constexpr uint16_t magic = 0xAA55;

    void parse_mbr(storage_dev::Device& dev);
} // namespace mbr
