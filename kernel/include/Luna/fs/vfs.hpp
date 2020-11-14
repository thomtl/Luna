#pragma once

#include <Luna/common.hpp>
#include <std/unordered_map.hpp>

namespace vfs {
    struct File {
        virtual ~File() {}

        virtual size_t read(size_t offset, size_t count, uint8_t* data) = 0;
        virtual size_t write(size_t offset, size_t count, uint8_t* data) = 0;
        virtual size_t get_size() = 0;

        virtual void close() = 0;
    };

    struct Filesystem {
        virtual ~Filesystem() {}
        virtual File* open(const char* path) = 0;
    };

    class Vfs {
        public:
        char mount(Filesystem* fs);
        void unmount(char fs);

        Filesystem& get_fs(char fs);

        File* open(const char* path);
        void close(File* file);

        private:
        Filesystem* mounted_fses[26]; // 26 Characters in the alfabet
    };

    Vfs& get_vfs();
} // namespace vfs