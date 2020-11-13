#include <Luna/drivers/storage/scsi.hpp>
#include <std/vector.hpp>

#include <Luna/misc/format.hpp>

static std::vector<scsi::Device*> devices;

std::pair<uint32_t, uint32_t> scsi_read_capacity(scsi::Device& device) {
    scsi::SCSICommand cmd{};
    cmd.write = false;

    auto* packet = (scsi::commands::read_capacity::Packet*)cmd.packet;
    packet->command = scsi::commands::read_capacity::command;

    scsi::commands::read_capacity::Response res{};

    std::span<uint8_t> xfer{(uint8_t*)&res, sizeof(res)};
    device.driver.scsi_cmd(device.driver.userptr, cmd, xfer);

    uint32_t block_size = bswap32(res.block_size);
    uint32_t lba = bswap32(res.lba) + 1; // Returned is the last lba, so add 1 for total

    return {lba, block_size};
}

void scsi_inquiry(scsi::Device& dev) {
    scsi::SCSICommand cmd{};
    auto& packet = *(scsi::commands::inquiry::Packet*)cmd.packet;

    packet.command = scsi::commands::inquiry::command;
    packet.flags.evpd = 0;
    packet.allocation_length = bswap16(128);

    uint8_t inquiry[128] = {};
    std::span<uint8_t> xfer{inquiry, 128};
    dev.driver.scsi_cmd(dev.driver.userptr, cmd, xfer);

    dev.type = inquiry[0] & 0x1F;
    switch (dev.type) {
        case 0: print("      Type: SBC-4\n"); break;
        case 1: print("      Type: SSC-3\n"); break;
        case 2: print("      Type: SSC\n"); break;
        case 3: print("      Type: SPC-2\n"); break;
        case 4: print("      Type: SBC\n"); break;
        case 5: print("      Type: MMC-5\n"); break;
        default: print("      Type: Unknown ({:#x})\n", (uint64_t)dev.type); break;
    }

    if(dev.type != 0 && dev.type != 5)
        return; // TODO: Support other device types

    dev.version = inquiry[2];
    
    char vendor_id[9] = {0};
    memcpy(vendor_id, inquiry + 8, 8);
    print("      Vendor: {:s}\n", vendor_id);

    char product_id[17] = {0};
    memcpy(product_id, inquiry + 16, 16);
    print("      Product: {:s}\n", product_id);

    const auto [lba, block_size] = scsi_read_capacity(dev);

    dev.n_sectors = lba;
    dev.sector_size = block_size;

    print("      Logical Sector Size: {} bytes\n", dev.sector_size);
    print("      Number of Sectors: {} [{} MiB]\n", dev.n_sectors, (dev.n_sectors * dev.sector_size) / 1024 / 1024);
}

void scsi_read12(scsi::Device& dev, uint32_t lba, uint32_t n_sectors, uint8_t* data) {
    scsi::SCSICommand cmd{};
    auto& packet = *(scsi::commands::read12::Packet*)cmd.packet;

    packet.command = scsi::commands::read12::command;
    packet.lba = bswap32(lba);
    packet.length = bswap32(n_sectors);

    std::span<uint8_t> xfer{data, dev.sector_size * n_sectors};
    dev.driver.scsi_cmd(dev.driver.userptr, cmd, xfer);
}

void scsi_read10(scsi::Device& dev, uint32_t lba, uint16_t n_sectors, uint8_t* data) {
    scsi::SCSICommand cmd{};
    auto& packet = *(scsi::commands::read10::Packet*)cmd.packet;

    packet.command = scsi::commands::read10::command;
    packet.lba = bswap32(lba);
    packet.length = bswap16(n_sectors);

    std::span<uint8_t> xfer{data, dev.sector_size * n_sectors};
    dev.driver.scsi_cmd(dev.driver.userptr, cmd, xfer);
}

void scsi::register_device(scsi::DriverDevice& dev) {
    auto* device = new Device{};
    devices.push_back(device);
    device->driver = dev;

    print("scsi: Registered device\n");

    scsi_inquiry(*device);
}