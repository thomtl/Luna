#pragma once

#include <Luna/common.hpp>
#include <Luna/fs/fs.hpp>
#include <Luna/fs/vfs.hpp>

#include <std/vector.hpp>

namespace echfs {
    struct [[gnu::packed]] Superblock {
        uint8_t reserved[4];
        char signature[8];
        uint64_t total_block_count;
        uint64_t main_dir_length;
        uint64_t bytes_per_block;
        uint32_t reserved_0;
        uint8_t uuid[16];
    };

    constexpr const char* signature = "_ECH_FS_";

    enum class ObjectType : uint8_t { File = 0, Directory = 1 };

    constexpr uint64_t end_of_directory_id = 0;
    constexpr uint64_t deleted_id = 0xFFFF'FFFF'FFFF'FFFE;
    constexpr uint64_t root_id = 0xFFFF'FFFF'FFFF'FFFF;

    constexpr uint64_t end_of_chain = 0xFFFF'FFFF'FFFF'FFFF;

    struct [[gnu::packed]] DirectoryEntry {
        uint64_t directory_id;
        ObjectType type;
        char name[201];
        uint64_t atime;
        uint64_t mtime;
        uint16_t permissions;
        uint16_t owner_id;
        uint16_t group_id;
        uint64_t ctime;
        uint64_t starting_block;
        uint64_t file_size;
    };

    bool probe(fs::Partition& part);

    struct Filesystem;

    struct File : public vfs::File {
        File(Filesystem* fs, const DirectoryEntry& entry, size_t i);
        size_t read(size_t offset, size_t count, uint8_t* data);
        size_t write(size_t offset, size_t count, uint8_t* data);
        size_t get_size();

        void close();

        private:
        size_t root_dir_index;
        DirectoryEntry entry;

        std::vector<uint64_t> fat_chain;

        Filesystem* fs;
    };

    struct Filesystem : public vfs::Filesystem {
        Filesystem(fs::Partition& part, const Superblock& superblock);
        vfs::File* open(const char* path);

        private:
        bool read_dirent(uint64_t i, DirectoryEntry& entry);
        uint64_t next_fat_entry(uint64_t fat_entry);
        
        uint64_t total_block_count;
        
        uint64_t bytes_per_block;

        uint64_t fat_block;
        size_t fat_n_blocks;

        uint64_t main_dir_block;
        size_t main_dir_n_blocks;

        uint64_t data_block;
        
        fs::Partition partition;

        friend struct File;
    };
} // namespace echfs
