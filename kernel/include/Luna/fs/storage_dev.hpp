#pragma once

#include <Luna/common.hpp>
#include <std/span.hpp>

#include <Luna/cpu/cpu.hpp>

#include <std/vector.hpp>
#include <std/mutex.hpp>

namespace storage_dev {
    struct DriverDevice {
        size_t n_lbas, sector_size;
        void* userptr;

        void (*xfer)(void* userptr, bool write, size_t lba, size_t n_lbas, std::span<uint8_t> xfer);
    };

    struct Device {
        Device(const DriverDevice& driver): driver{driver} {
            for(size_t i = 0; i < max_cache_size; i++)
                cache.emplace_back(driver.sector_size);
        }

        DriverDevice driver;

        bool read(size_t offset, size_t count, uint8_t* data);
        bool write(size_t offset, size_t count, uint8_t* data);

        private:
        struct CacheBlock {
            CacheBlock(size_t sector_size): timestamp{0}, block{~0ull}, buffer{}, modified{false} {
                buffer.resize(sector_size * sectors_per_cache_block);
            }

            uint64_t timestamp, block;
            std::vector<uint8_t> buffer;
            bool modified;
        };
        CacheBlock& get_cache_block(size_t block);

        std::vector<CacheBlock> cache;
        uint64_t curr_timestamp;
        constexpr static size_t max_cache_size = 256;
        constexpr static size_t sectors_per_cache_block = 64;

        IrqTicketLock lock;
    };

    void register_device(const DriverDevice& driver);
} // namespace fs
