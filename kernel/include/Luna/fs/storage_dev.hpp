#pragma once

#include <Luna/common.hpp>
#include <std/span.hpp>

namespace storage_dev {
    struct DriverDevice {
        size_t n_lbas, sector_size;
        void* userptr;

        void (*xfer)(void* userptr, bool write, size_t lba, size_t n_lbas, std::span<uint8_t>& xfer);
    };

    struct Device {
        DriverDevice driver;

        bool read(size_t offset, size_t count, uint8_t* data);
        bool write(size_t offset, size_t count, uint8_t* data);
    };

    void register_device(const DriverDevice& driver);

    
} // namespace fs
