#include <Luna/fs/gpt.hpp>

#include <Luna/fs/fs.hpp>

#include <Luna/misc/log.hpp>
#include <std/string.hpp>

static bool check_header_integrity(const gpt::GPTHeader& header, uint64_t lba) {
    if(strncmp(header.signature, gpt::header_sig, 8) != 0) {
        print("gpt: Unknown header signature");
        return false;
    }

    // TODO: Check header CRC

    if(header.my_lba != lba) {
        print("gpt: Header MyLBA is invalid: {}\n", (uint64_t)header.my_lba);
        return false;
    }

    return true;
}

void gpt::parse_gpt(storage_dev::Device& dev) {
    GPTHeader header;
    dev.read(dev.driver.sector_size, sizeof(GPTHeader), (uint8_t*)&header);
    

    if(check_header_integrity(header, 1)) {
        GPTHeader secondary;
        dev.read(dev.driver.sector_size * header.alternate_lba, sizeof(GPTHeader), (uint8_t*)&secondary);

        if(!check_header_integrity(secondary, header.alternate_lba)) {
            print("gpt: Secondary header failed integrity checks, ignoring...\n");
        }
    } else {
        uint64_t secondary_lba = dev.driver.n_lbas - 1;
        dev.read(secondary_lba * dev.driver.sector_size, sizeof(GPTHeader), (uint8_t*)&header);

        if(!check_header_integrity(header, secondary_lba)) {
            print("gpt: Both primary and secondary header failed integrity check, aborting..\n");
        }

        // TODO: Restore primary header
    }

    // TODO: Do partition table CRC


    constexpr UUID empty_partition{"00000000-0000-0000-0000-000000000000"};
    constexpr UUID efi_system_partition{"C12A7328-F81F-11D2-BA4B-00A0C93EC93B"};
    constexpr UUID microsoft_basic_data{"EBD0A0A2-B9E5-4433-87C0-68B6B72699C7"};


    for(size_t i = 0; i < header.number_of_partition_entries; i++ ) {
        uint64_t offset = (header.partition_entry_lba * dev.driver.sector_size) + (i * header.size_of_partition_entry);
        GPTPartitionEntry entry{};
        dev.read(offset, sizeof(GPTPartitionEntry), (uint8_t*)&entry);

        UUID type{std::span<uint8_t, 16>{entry.type_guid}};

        if(type == empty_partition || type == efi_system_partition) {
            continue;
        } else if(type == microsoft_basic_data) {
            // TODO: Abide by attributes

            fs::Partition partition{};
            partition.device = &dev;
            partition.n_sectors = entry.ending_lba - entry.starting_lba;
            partition.start_lba = entry.starting_lba;

            fs::probe_fs(partition);
        } else {
            char buf[64] = "";
            print("gpt: Unknown partition GUID type: {}\n", type.to_string(buf));
        }
    } 
}