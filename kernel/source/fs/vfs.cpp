#include <Luna/fs/vfs.hpp>

vfs::Vfs& vfs::get_vfs() {
    static std::lazy_initializer<vfs::Vfs> vfs;

    if(!vfs)
        vfs.init();
    
    return *vfs;
}

constexpr uint8_t index_for_char(char c) {
    if(c >= 'a' && c <= 'z')
        return c - 'a';
    else
        return c - 'A';
}

constexpr char char_for_index(uint8_t i) {
    return 'A' + i;
}

char vfs::Vfs::mount(Filesystem* mount) {
    for(uint8_t i = 0; i < 26; i++) {
        if(mounted_fses[i] == nullptr) {
            mounted_fses[i] = mount;
            return char_for_index(i);
        }
    }

    return ' '; // TODO: std::optional or something
}

void vfs::Vfs::unmount(char fs) {
    auto* ptr = mounted_fses[index_for_char(fs)];
    mounted_fses[index_for_char(fs)] = nullptr;

    delete ptr; // TODO: Should we do this here
}

vfs::Filesystem& vfs::Vfs::get_fs(char fs) {
    auto* ptr = mounted_fses[index_for_char(fs)];
    if(ptr)
        return *ptr;

    PANIC("No fs for char");
}

vfs::File* vfs::Vfs::open(const char* path) {
    // Path spec: A:Path, where A is the char of the fs and path is the fs relative path
    return get_fs(path[0]).open(path + 2); // Skip first 2 chars in the path
}

void vfs::Vfs::close(vfs::File* file) {
    file->close();

    delete file;
}