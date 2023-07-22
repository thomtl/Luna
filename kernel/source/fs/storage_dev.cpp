#include <Luna/fs/storage_dev.hpp>

#include <Luna/fs/mbr.hpp>
#include <Luna/fs/gpt.hpp>

#include <Luna/misc/log.hpp>

#include <std/linked_list.hpp>
#include <std/string.hpp>
#include <std/algorithm.hpp>

storage_dev::Device::CacheBlock& storage_dev::Device::get_cache_block(size_t block) {
    auto evict = [this](CacheBlock& entry) {
        if(!entry.modified)
            return;
        
        driver.xfer(driver.userptr, true, entry.block * sectors_per_cache_block, sectors_per_cache_block, std::span<uint8_t>{entry.buffer.data(), sectors_per_cache_block * driver.sector_size});
        entry.modified = false;
    };

    auto get_lru = [this] {
        uint64_t oldest = ~0ull;
        auto ret = cache.end();

        for(auto it = cache.begin(); it != cache.end(); ++it) {
            if(it->timestamp < oldest) {
                oldest = it->timestamp;
                ret = it;
            }
        }

        return ret;
    };

    auto it = std::find_if(cache.begin(), cache.end(), [block](auto& e) { return (e.block == block); });
    if(it == cache.end()) {
        it = get_lru();
        ASSERT(it != cache.end());
        evict(*it);

        it->block = block;

        driver.xfer(driver.userptr, false, block * sectors_per_cache_block, sectors_per_cache_block, std::span<uint8_t>{it->buffer.data(), sectors_per_cache_block * driver.sector_size});
    }
    it->timestamp = ++curr_timestamp;

    return *it;
}

bool storage_dev::Device::read(size_t offset, size_t count, uint8_t* data) {
    auto dev_size = driver.n_lbas * driver.sector_size;
    if(offset > dev_size)
        return false;
    
    if((offset + count) > dev_size)
        count = dev_size - offset;
    
    std::lock_guard guard{lock};

    auto bytes_per_block = sectors_per_cache_block * driver.sector_size;

    size_t progress = 0;
    while(progress < count) {
        size_t block = (offset + progress) / bytes_per_block;
        auto& cache = get_cache_block(block);

        uint64_t off = (offset + progress) % bytes_per_block;
        uint64_t chunk = min(count - progress, bytes_per_block - off);

        memcpy(data + progress, cache.buffer.data() + off, chunk);
        progress += chunk;
    }

    return true;
}

bool storage_dev::Device::write(size_t offset, size_t count, uint8_t* data) {
    auto dev_size = driver.n_lbas * driver.sector_size;
    if(offset > dev_size)
        return false;
    
    if((offset + count) > dev_size)
        count = dev_size - offset;

    std::lock_guard guard{lock};

    auto bytes_per_block = sectors_per_cache_block * driver.sector_size;

    size_t progress = 0;
    while(progress < count) {
        size_t block = (offset + progress) / bytes_per_block;
        auto& cache = get_cache_block(block);
        cache.modified = true;

        uint64_t off = (offset + progress) % bytes_per_block;
        uint64_t chunk = min(count - progress, bytes_per_block - off);

        memcpy(cache.buffer.data() + off, data + progress, chunk);
        progress += chunk;
    }

    return true;
}

static std::linked_list<storage_dev::Device*> devices;
static IrqTicketLock lock{};

void storage_dev::register_device(const DriverDevice& driver) {
    std::lock_guard guard{lock};
    
    auto* device = new Device{driver};
    devices.emplace_back(device);


    // TODO: More robust detection?
    uint8_t magic[8] = {0};
    device->read(driver.sector_size, 8, magic); // LBA1
    if(strncmp((char*)magic, "EFI PART", 8) == 0)
        gpt::parse_gpt(*device);
    else
        mbr::parse_mbr(*device);
}