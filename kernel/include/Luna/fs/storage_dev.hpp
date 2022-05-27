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
        void xfer_sectors(bool write, size_t lba, size_t count, uint8_t* data);

        struct PageCache {
            PageCache(size_t sector_size): timestamp{0}, lba{~0ull}, buffer{}, modified{false} {
                buffer.resize(sector_size);
            }

            uint64_t timestamp, lba;
            std::vector<uint8_t> buffer;
            bool modified;
        };
        std::vector<PageCache> cache;
        uint64_t curr_timestamp;
        constexpr static size_t max_cache_size = 64;

        IrqTicketLock lock;
    };

    void register_device(const DriverDevice& driver);
} // namespace fs
