#pragma once

#include <Luna/common.hpp>
#include <Luna/fs/storage_dev.hpp>


namespace fs {
    struct Partition {
        storage_dev::Device* device;
        size_t start_lba, n_sectors;

        void read(size_t offset, size_t count, uint8_t* data) {
            device->read((device->driver.sector_size * start_lba) + offset, count, data);
        }

        void write(size_t offset, size_t count, uint8_t* data) {
            device->write((device->driver.sector_size * start_lba) + offset, count, data);
        }
    };

    void probe_fs(const Partition& part);
} // namespace fs