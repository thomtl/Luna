#include <Luna/fs/echfs.hpp>

#include <Luna/misc/log.hpp>

#include <std/string.hpp>

bool echfs::probe(fs::Partition& part) {
    Superblock superblock{};
    part.read(0, sizeof(superblock), (uint8_t*)&superblock);

    if(strncmp(superblock.signature, signature, 8) != 0)
        return false;

    auto point = vfs::get_vfs().mount(new Filesystem{part, superblock});
    print("echfs: Mounting echfs partition on {:c}\n", point);
    
    return true;
}

echfs::Filesystem::Filesystem(fs::Partition& part, const Superblock& superblock): partition{part} {
    bytes_per_block = superblock.bytes_per_block;
    total_block_count = superblock.total_block_count;

    fat_block = 16;
    fat_n_blocks = div_ceil(total_block_count * sizeof(uint64_t), bytes_per_block);

    main_dir_block = fat_block + fat_n_blocks;
    main_dir_n_blocks = superblock.main_dir_length;

    data_block = 16 + fat_n_blocks + main_dir_n_blocks;
    
    /*print("echfs: Partition Info\n");
    print("       FAT {} -> {}\n", fat_block, fat_block + fat_n_blocks);
    print("       Dir {} -> {}\n", main_dir_block, main_dir_block + main_dir_n_blocks);
    print("       Data {} -> End\n", data_block);*/
}

vfs::File* echfs::Filesystem::open(const char* path) {
    auto search = [&](const char* name, uint64_t dir_id, ObjectType type) -> uint64_t {
        size_t i = 0;
        DirectoryEntry entry{};
        while(read_dirent(i, entry)) {
            if(entry.directory_id == end_of_directory_id)
                return ~0;

            if(entry.directory_id == dir_id && entry.type == type && strncmp(name, entry.name, 200) == 0)
                return i;
            
            i++;
        }

        return ~0;
    };

    if(*path == '/')
        path++;

    uint64_t curr_id = root_id;

    DirectoryEntry entry{};
    while(true) {
        char name[200] = {0};
        bool last = false;
        size_t i = 0;

        for(i = 0; *path != '/'; path++) {
            if(*path == '\0') {
                last = true;
                break;
            }

            name[i++] = *path;
        }

        name[i] = '\0';
        path++;

        if(!last) {
            auto res = search(name, curr_id, ObjectType::Directory);
            if(res == ~0ull)
                return nullptr; // Does not exist
            
            ASSERT(read_dirent(res, entry));

            curr_id = entry.starting_block; // Directory-ID for dirs
        } else {
            if(strlen(name) != 0) { // If name == 0, its a dir and we already found the entry
                auto res = search(name, curr_id, ObjectType::File);
                if(res == ~0ull) {
                    return nullptr; // Does not exist
                }

                ASSERT(read_dirent(res, entry));

                curr_id = res;
            }
            break;
        }
    }

    return new echfs::File{this, entry, curr_id};
}

bool echfs::Filesystem::read_dirent(uint64_t i, DirectoryEntry& entry) {
    auto loc = (main_dir_block * bytes_per_block) + (sizeof(DirectoryEntry) * i);
    if(loc >= ((main_dir_block * bytes_per_block) + (main_dir_n_blocks * bytes_per_block)))
        return false;

    partition.read(loc, sizeof(DirectoryEntry), (uint8_t*)&entry);

    return true;
}

uint64_t echfs::Filesystem::next_fat_entry(uint64_t fat_entry) {
    auto loc = (fat_block * bytes_per_block) + (fat_entry * sizeof(uint64_t));
    if(loc >= ((fat_block * bytes_per_block) + (fat_n_blocks * bytes_per_block)))
        return -1;
    
    uint64_t ret = end_of_chain;
    partition.read(loc, 8, (uint8_t*)&ret);

    return ret;
}


echfs::File::File(Filesystem* fs, const DirectoryEntry& entry, size_t i): root_dir_index{i}, entry{entry}, fs{fs} {
    if(entry.type == ObjectType::File) {
        fat_chain.push_back(entry.starting_block);

        size_t j = 1;
        for(; fat_chain[j - 1] != end_of_chain; j++)
            fat_chain.push_back(fs->next_fat_entry(fat_chain[j - 1]));
    }
}

vfs::FileType echfs::File::get_type() {
    if(entry.type == ObjectType::Directory)
        return vfs::FileType::Directory;
    else if(entry.type == ObjectType::File)
        return vfs::FileType::File;

    PANIC("Unknown Filetype");
}

size_t echfs::File::read(size_t offset, size_t count, uint8_t* data) {
    if(entry.type != ObjectType::File)
        return 0;
    
    if((offset + count) >= entry.file_size)
        count = entry.file_size - offset;

    uint64_t progress = 0;
    while(progress < count) {
        uint64_t block = (offset + progress) / fs->bytes_per_block;
        uint64_t loc = fat_chain[block] * fs->bytes_per_block;

        uint64_t chunk = count - progress;
        uint64_t disk_offset = (offset + progress) % fs->bytes_per_block;
        if(chunk > (fs->bytes_per_block - disk_offset))
            chunk = (fs->bytes_per_block - disk_offset);

        fs->partition.read(loc + disk_offset, chunk, data + progress);
        progress += chunk;
    }

    return count;
}

size_t echfs::File::write(size_t offset, size_t count, uint8_t* data) {
    PANIC("TODO: Implement file writing");
    (void)offset;
    (void)count;
    (void)data;
    return 0;
}

size_t echfs::File::get_size() {
    if(entry.type != ObjectType::File)
        return 0;
    
    return entry.file_size;
}

void echfs::File::close() {
    // Nothing To Do, no caches, no resources to release
}