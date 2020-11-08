#include <Luna/drivers/storage/ata.hpp>
#include <std/vector.hpp>

#include <Luna/misc/format.hpp>

std::vector<ata::Device> devices;



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

        device.sector_size = 512; // TODO: Don't assume this but send the command to enumerate it
    } else {
        // TODO: Send PIReadCapacity command
    }

    print("     Number of Sectors {} [{} MiB]\n", device.n_sectors, (device.n_sectors * device.sector_size) / 1024 / 1024);
}


void ata::register_device(ata::DriverDevice& dev) {
    auto& device = devices.emplace_back();
    device.driver = dev;

    print("ata: Registered Device\n");
    identify_drive(device);
}