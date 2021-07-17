#include <Luna/fs/storage_dev.hpp>

#include <Luna/fs/mbr.hpp>

#include <Luna/misc/log.hpp>

#include <std/linked_list.hpp>
#include <std/string.hpp>

bool storage_dev::Device::read(size_t offset, size_t count, uint8_t* data) {
    std::lock_guard guard{lock};

    size_t start_lba = offset / driver.sector_size;
    size_t end_lba = (offset + count - 1) / driver.sector_size;

    size_t sector_size = driver.sector_size;
    size_t dev_size = (driver.n_lbas * driver.sector_size);
    size_t x_offset = 0;

    if(offset > dev_size)
        return false; // Beyond range

    if((offset + count) > dev_size)
        count = dev_size - offset;

    if(offset % sector_size || count < sector_size) {
        size_t prefix_size = sector_size - (offset % sector_size);

        if(prefix_size > count)
            prefix_size = count;
        
        auto* sector = new uint8_t[sector_size];

        std::span<uint8_t> xfer{sector, sector_size};
        driver.xfer(driver.userptr, false, start_lba, 1, xfer);

        memcpy(data, sector + (offset % sector_size), prefix_size);

        delete[] sector;

        x_offset += prefix_size;
        start_lba++;
    }

    if((offset + count) % sector_size && start_lba <= end_lba) {
        size_t postfix_size = (offset + count) % sector_size;

        auto* sector = new uint8_t[sector_size];

        std::span<uint8_t> xfer{sector, sector_size};
        driver.xfer(driver.userptr, false, end_lba, 1, xfer);

        memcpy(data + count - postfix_size, sector, postfix_size);

        delete[] sector;

        end_lba--;
    }

    size_t n_sectors = end_lba - start_lba + 1;
    if(n_sectors > 0) {
        std::span<uint8_t> xfer{data + x_offset, sector_size * n_sectors};
        driver.xfer(driver.userptr, false, start_lba, n_sectors, xfer);
    }
    
    return true;
}

bool storage_dev::Device::write(size_t offset, size_t count, uint8_t* data) {
    std::lock_guard guard{lock};
    
    size_t start_lba = offset / driver.sector_size;
    size_t end_lba = (offset + count - 1) / driver.sector_size;

    size_t sector_size = driver.sector_size;
    size_t dev_size = (driver.n_lbas * driver.sector_size);
    size_t x_offset = 0;

    if(offset > dev_size)
        return false; // Beyond range

    if((offset + count) > dev_size)
        count = dev_size - offset;

    if(offset % sector_size) {
        size_t prefix_size = sector_size - (offset % sector_size);

        if(prefix_size > count)
            prefix_size = count;

        auto* sector = new uint8_t[sector_size];

        std::span<uint8_t> xfer{sector, sector_size};
        driver.xfer(driver.userptr, false, start_lba, 1, xfer);
        memcpy(sector + (offset % sector_size), data, prefix_size);
        driver.xfer(driver.userptr, true, start_lba, 1, xfer);

        delete[] sector;

        x_offset += prefix_size;
        start_lba++;
    }

    if((offset + count) % sector_size && start_lba <= end_lba) {
        size_t postfix_size = (offset + count) % sector_size;

        auto* sector = new uint8_t[sector_size];

        std::span<uint8_t> xfer{sector, sector_size};
        driver.xfer(driver.userptr, false, end_lba, 1, xfer);
        memcpy(sector, data + count - postfix_size, postfix_size);
        driver.xfer(driver.userptr, true, end_lba, 1, xfer);

        delete[] sector;
        end_lba--;
    }

    size_t n_sectors = end_lba - start_lba + 1;
    if(n_sectors > 0) {
        std::span<uint8_t> xfer{data + x_offset, sector_size * n_sectors};
        driver.xfer(driver.userptr, true, start_lba, n_sectors, xfer);   
    }

    return true;
}

static std::linked_list<storage_dev::Device*> devices;
static IrqTicketLock lock{};

void storage_dev::register_device(const DriverDevice& driver) {
    std::lock_guard guard{lock};
    
    auto* device = new Device{};
    devices.emplace_back(device);
    device->driver = driver;

    uint8_t magic[8] = {0};
    device->read(driver.sector_size, 8, magic); // LBA1
    if(strncmp((char*)magic, "EFI PART", 8) == 0)
        print("disk: TODO: Implement GPT\n");
    else
        mbr::parse_mbr(*device);
}