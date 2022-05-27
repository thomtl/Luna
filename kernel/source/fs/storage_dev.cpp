#include <Luna/fs/storage_dev.hpp>

#include <Luna/fs/mbr.hpp>

#include <Luna/misc/log.hpp>

#include <std/linked_list.hpp>
#include <std/string.hpp>
#include <std/algorithm.hpp>

void storage_dev::Device::xfer_sectors(bool write, size_t lba, size_t count, uint8_t* data) {
    auto evict_cache = [&](PageCache& entry) {
        if(!entry.modified)
            return;
        
        driver.xfer(driver.userptr, true, entry.lba, 1, std::span<uint8_t>{entry.buffer.data(), driver.sector_size});
        entry.modified = false;
    };

    auto get_lru_cache = [&]() -> PageCache& {
        uint64_t oldest_timestamp = ~0;
        PageCache* oldest = nullptr;
        for(auto& entry : cache) {
            if(entry.timestamp < oldest_timestamp) {
                oldest = &entry;
                oldest_timestamp = entry.timestamp;
            }
        }
        
        ASSERT(oldest);
        return *oldest;
    };

    for(size_t curr = lba; curr < (lba + count); curr++) {
        auto it = std::find_if(cache.begin(), cache.end(), [curr](auto& e) { return e.lba == curr; });

        if(it != cache.end()) {
            if(write) {
                memcpy(it->buffer.data(), data, driver.sector_size);
                it->modified = true;
            } else {
                memcpy(data, it->buffer.data(), driver.sector_size);
            }
        } else {
            auto& entry = get_lru_cache();
            evict_cache(entry);
            if(write) {
                entry.lba = curr;
                entry.modified = true;
                entry.timestamp = ++curr_timestamp;

                memcpy(it->buffer.data(), data, driver.sector_size);
            } else {
                entry.lba = curr;
                entry.timestamp = ++curr_timestamp;

                driver.xfer(driver.userptr, false, curr, 1, std::span<uint8_t>{entry.buffer.data(), driver.sector_size});
                memcpy(data, entry.buffer.data(), driver.sector_size);
            }
        }

        data += driver.sector_size;
    }
}

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

        xfer_sectors(false, start_lba, 1, sector);

        memcpy(data, sector + (offset % sector_size), prefix_size);

        delete[] sector;

        x_offset += prefix_size;
        start_lba++;
    }

    if((offset + count) % sector_size && start_lba <= end_lba) {
        size_t postfix_size = (offset + count) % sector_size;

        auto* sector = new uint8_t[sector_size];

        xfer_sectors(false, end_lba, 1, sector);

        memcpy(data + count - postfix_size, sector, postfix_size);

        delete[] sector;

        end_lba--;
    }

    size_t n_sectors = end_lba - start_lba + 1;
    if(n_sectors > 0)
        xfer_sectors(false, start_lba, n_sectors, data + x_offset);
    
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
        
        xfer_sectors(false, start_lba, 1, sector);
        memcpy(sector + (offset % sector_size), data, prefix_size);
        xfer_sectors(true, start_lba, 1, sector);

        delete[] sector;

        x_offset += prefix_size;
        start_lba++;
    }

    if((offset + count) % sector_size && start_lba <= end_lba) {
        size_t postfix_size = (offset + count) % sector_size;

        auto* sector = new uint8_t[sector_size];

        xfer_sectors(false, end_lba, 1, sector);
        memcpy(sector, data + count - postfix_size, postfix_size);
        xfer_sectors(true, end_lba, 1, sector);

        delete[] sector;
        end_lba--;
    }

    size_t n_sectors = end_lba - start_lba + 1;
    if(n_sectors > 0)
        xfer_sectors(true, start_lba, n_sectors, data + x_offset); 

    return true;
}

static std::linked_list<storage_dev::Device*> devices;
static IrqTicketLock lock{};

void storage_dev::register_device(const DriverDevice& driver) {
    std::lock_guard guard{lock};
    
    auto* device = new Device{driver};
    devices.emplace_back(device);


    uint8_t magic[8] = {0};
    device->read(driver.sector_size, 8, magic); // LBA1
    if(strncmp((char*)magic, "EFI PART", 8) == 0)
        print("disk: TODO: Implement GPT\n");
    else
        mbr::parse_mbr(*device);
}