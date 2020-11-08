#include <Luna/drivers/storage/ata.hpp>
#include <std/vector.hpp>

#include <Luna/misc/format.hpp>

std::vector<ata::Device> devices;

std::pair<uint32_t, uint32_t> pi_read_capacity(ata::Device& device) {
    ASSERT(device.driver.atapi);

    ata::ATAPICommand cmd{};

    cmd.write = false;
    auto* packet = (ata::packet_commands::read_capacity::packet*)cmd.packet;

    packet->command = ata::packet_commands::read_capacity::command;

    ata::packet_commands::read_capacity::response res{};

    std::span<uint8_t> xfer{(uint8_t*)&res, sizeof(res)};
    device.driver.atapi_cmd(device.driver.userptr, cmd, xfer);

    uint32_t block_size = bswap32(res.block_size);
    uint32_t lba = bswap32(res.lba);

    return {lba, block_size};
}

void identify_drive(ata::Device& device) {
    ata::ATACommand cmd{};
    cmd.command = device.driver.atapi ? ata::commands::IdentifyPI : ata::commands::Identify;

    std::span<uint8_t> xfer{device.identify, 512};
    device.driver.ata_cmd(device.driver.userptr, cmd, xfer);

    auto checksum = [&]() -> bool {
        if(xfer[510] == 0xA5) {
            uint8_t checksum = 0;
            for(size_t i = 0; i < 511; i++)
                checksum += xfer[i];

            return ((uint8_t)-checksum == xfer[511]);
        } else if(xfer[510] != 0) {
            return false;
        } else {
            return true; // Probably a version older than ATA-5 which didn't have checksums
        }
    };

    if(!checksum()) {
        print("     Failed Identify checksum\n");
        return;
    }

    char model[41] = {};
    memcpy(model, &xfer[54], 40);
    model[40] = '\0';

    for(size_t i = 0; i < 40; i += 2) {
        auto tmp = model[i];
        model[i] = model[i + 1];
        model[i + 1] = tmp;
    }
    print("     Model: {}\n", model);

    device.lba48 = (xfer[167] & (1 << 2)) && (xfer[173] & (1 << 2));

    if(!device.driver.atapi) {
        device.n_sectors = *(uint64_t*)(xfer.data() + 200);
        if(device.n_sectors == 0)
            device.n_sectors = *(uint64_t*)(xfer.data() + 120);

        auto sector_size = *(uint16_t*)(xfer.data() + (106 * 2));
        if(sector_size & (1 << 14) && !(sector_size & (1 << 15))) { // Word contains valid info
            if(sector_size & (1 << 12)) { // Logical sectors are larger than 512
                device.sector_size = *(uint16_t*)(xfer.data() + (117 * 2));
            } else {
                device.sector_size = 512; // TODO: Don't assume this but send the command to enumerate it
            }
        } else {
            device.sector_size = 512; // TODO: Don't assume this but send the command to enumerate it
        }
        device.inserted = true;
    } else {
        const auto [lba, sector_size] = pi_read_capacity(device);

        device.n_sectors = lba;
        device.sector_size = sector_size;

        device.inserted = (lba != 0 && sector_size != 0);
    }

    if(device.inserted) {
        print("     Logical Sector Size: {} bytes\n", device.sector_size);
        print("     Number of Sectors: {} [{} MiB]\n", device.n_sectors, (device.n_sectors * device.sector_size) / 1024 / 1024);
    } else {
        print("     Device is not inserted\n");
    }
}


void ata::register_device(ata::DriverDevice& dev) {
    auto& device = devices.emplace_back();
    device.driver = dev;

    print("ata: Registered {} Device\n", dev.atapi ? "ATAPI" : "ATA");

    ASSERT(dev.ata_cmd);
    if(dev.atapi)
        ASSERT(dev.atapi_cmd);

    identify_drive(device);
}