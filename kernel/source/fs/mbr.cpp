#include <Luna/fs/mbr.hpp>

#include <Luna/fs/fs.hpp>

#include <Luna/misc/log.hpp>

void mbr::parse_mbr(storage_dev::Device& dev) {
    PartitionTable table{};
    dev.read(0x1BE, sizeof(table), (uint8_t*)&table);

    if(table.magic != magic) {
        auto magic = table.magic;
        print("mbr: Invalid magic {:#x}\n", magic);
        return;
    }

    for(const auto part : table.entries) {
        if(part.type == 0)
            continue; // Inactive

        fs::Partition partition{};
        partition.device = &dev;
        partition.n_sectors = part.n_sectors;
        partition.start_lba = part.lba_start;

        fs::probe_fs(partition);
    }
}