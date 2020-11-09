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

    auto print_string = [&xfer](const char* prefix, size_t off, size_t len){
        ASSERT((len & 1) == 0); // Assert len is divisible by 2
        ASSERT(len <= 40); // Can't use a VLA in a template pack???
        char buf[41] = {};
        memcpy(buf, &xfer[off], len);
        buf[len] = '\0';

        for(size_t i = 0; i < len; i += 2) {
            auto tmp = buf[i];
            buf[i] = buf[i + 1];
            buf[i + 1] = tmp;
        }

        print("{}{}\n", prefix, buf);
    };

    print_string("     Model: ", 54, 40);
    print_string("     Serial: ", 20, 20);
    print_string("     FW Revision: ", 46, 8);

    

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

        device.n_sectors = lba + 1;
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

bool verify_lba(const ata::Device& device, uint64_t lba) {
    if(device.lba48)
        return ((lba & ~0xFFFF'FFFF'FFFF) == 0);
    else
        return ((lba & ~0xFFF'FFFF) == 0);
}

bool read_sectors(ata::Device& device, uint64_t lba, size_t n_sectors, uint8_t* data) {
    if(device.driver.atapi)
        PANIC("TODO: Implement ATAPI reading");

    if(!verify_lba(device, lba))
        return false; // Invalid LBA for device

    ata::ATACommand cmd{};
    cmd.command = device.lba48 ? ata::commands::ReadExtendedDMA : ata::commands::ReadDMA;
    cmd.lba = lba;
    cmd.n_sectors = n_sectors;
    cmd.write = false;
    cmd.lba28 = !device.lba48; // We need this info to write the correct top lba28 nybble

    std::span<uint8_t> xfer{data, device.sector_size * n_sectors};
    device.driver.ata_cmd(device.driver.userptr, cmd, xfer);

    return true;
}

bool write_sectors(ata::Device& device, uint64_t lba, size_t n_sectors, uint8_t* data) {
    if(device.driver.atapi)
        PANIC("TODO: Implement ATAPI writing");

    if(!verify_lba(device, lba))
        return false; // Invalid LBA for device

    ata::ATACommand cmd{};
    cmd.command = device.lba48 ? ata::commands::WriteExtendedDMA : ata::commands::WriteDMA;
    cmd.lba = lba;
    cmd.n_sectors = n_sectors;
    cmd.write = true;
    cmd.lba28 = !device.lba48; // We need this info to write the correct top lba28 nybble

    std::span<uint8_t> xfer{data, device.sector_size * n_sectors};
    device.driver.ata_cmd(device.driver.userptr, cmd, xfer);

    return true;
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