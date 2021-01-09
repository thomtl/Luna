#include <Luna/fs/fs.hpp>
#include <Luna/misc/log.hpp>

#include <Luna/fs/echfs.hpp>

void fs::probe_fs(fs::Partition& part) {
    if(echfs::probe(part))
        return;

    print("fs: Unknown partition on disk\n");
}