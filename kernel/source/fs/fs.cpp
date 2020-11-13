#include <Luna/fs/fs.hpp>
#include <Luna/misc/format.hpp>

void fs::probe_fs(const fs::Partition& part) {
    (void)(part);
    // TODO: Probe here

    print("fs: Unknown partition on disk\n");
}