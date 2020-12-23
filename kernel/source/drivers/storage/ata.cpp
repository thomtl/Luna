#include <Luna/drivers/storage/ata.hpp>
#include <std/linked_list.hpp>

#include <Luna/misc/format.hpp>
#include <Luna/drivers/storage/scsi.hpp>

#include <Luna/fs/storage_dev.hpp>

static std::linked_list<ata::Device*> devices;

void identify_drive(ata::Device& device) {
    ata::ATACommand cmd{};
    cmd.command = device.driver.atapi ? ata::commands::IdentifyPI : ata::commands::Identify;
    
    uint8_t identify[512] = {};
    std::span<uint8_t> xfer{identify, 512};
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
                device.sector_size = 512;
            }
        } else {
            device.sector_size = 512;
        }
    } else {
        uint8_t packet_size = xfer[0] & 0b11;
        if(packet_size == 0b00)
            device.max_packet_size = 12;
        else if(packet_size == 0b01)
            device.max_packet_size = 16;
        else
            print("ata: Unknown packet size {:#b}\n", (uint64_t)packet_size);
    }

    if(device.driver.atapi) {
        print("     Max Packet Size: {} bytes\n", (uint64_t)device.max_packet_size);
    } else {
        print("     Logical Sector Size: {} bytes\n", device.sector_size);
        print("     Number of Sectors: {} [{} MiB]\n", device.n_sectors, (device.n_sectors * device.sector_size) / 1024 / 1024);
    }
}

bool verify_lba(const ata::Device& device, uint64_t lba) {
    if(device.lba48)
        return ((lba & ~0xFFFF'FFFF'FFFF) == 0);
    else
        return ((lba & ~0xFFF'FFFF) == 0);
}

bool read_sectors(ata::Device& device, uint64_t lba, size_t n_sectors, uint8_t* data) {
    if(n_sectors == 0)
        return true;
    
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
    if(n_sectors == 0)
        return true;

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
    auto* device = new Device{};
    devices.emplace_back(device);
    device->driver = dev;

    print("ata: Registered {} Device\n", dev.atapi ? "ATAPI" : "ATA");

    ASSERT(dev.ata_cmd);
    if(dev.atapi)
        ASSERT(dev.atapi_cmd);

    identify_drive(*device);  

    // Register with SCSI driver if ATAPI
    if(dev.atapi) {
        scsi::DriverDevice scsi_dev{};
        scsi_dev.max_packet_size = device->max_packet_size;
        scsi_dev.userptr = device;
        scsi_dev.scsi_cmd = [](void* userptr, const scsi::SCSICommand& cmd, std::span<uint8_t>& xfer) {
            auto& device = *(Device*)userptr;

            ATAPICommand atapi_cmd{};
            memcpy(atapi_cmd.packet, cmd.packet, 16);
            atapi_cmd.write = cmd.write;

            device.driver.atapi_cmd(device.driver.userptr, atapi_cmd, xfer);
        };

        scsi::register_device(scsi_dev);
    }

    // ATAPI Devices are finally registed in scsi.cpp
    if(!dev.atapi) {
        storage_dev::DriverDevice driver{};
        driver.n_lbas = device->n_sectors;
        driver.sector_size = device->sector_size;
        driver.userptr = device;

        driver.xfer = [](void* userptr, bool write, size_t lba, size_t n_lbas, std::span<uint8_t>& xfer) {
            auto& device = *(ata::Device*)userptr;

            if(write)
                write_sectors(device, lba, n_lbas, xfer.data());
            else
                read_sectors(device, lba, n_lbas, xfer.data());
        };
        storage_dev::register_device(driver);
    }
}