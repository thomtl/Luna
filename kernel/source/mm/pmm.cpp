#include <Luna/mm/pmm.hpp>
#include <Luna/misc/format.hpp>

static std::span<uint8_t> bitmap;
static size_t bitmap_size;

void pmm::init(stivale2::Parser& parser) {
    size_t memory_size = 0, highest_page = 0;

    print("pmm: Stivale Memory Map:\n");
    for(auto& entry : parser.mmap()) {
        const auto e = entry;
        print("   - {:#x} - {:#x}: {}\n", e.base, e.base + e.length, stivale2::mmap_type_to_string(e.type));

        if(entry.type == STIVALE2_MMAP_USABLE) {
            memory_size += entry.length;

            auto base = align_up(entry.base, 0x1000);
            auto size = align_down(entry.length - (base - entry.base), 0x1000);
            auto top = base + size;

            entry.base = base;
            entry.length = size;

            if(top > highest_page)
                highest_page = top;
        }
    }

    print("pmm: Detected {} MiBs of usable RAM\n", memory_size / 1024 / 1024);

    bitmap_size = (highest_page / 0x1000) / 8;
    print("pmm: Highest usable address: {:#x} => Bitmap size: {:#x} bytes\n", highest_page, bitmap_size);

    bool bitmap_constructed = false;
    for(auto& entry : parser.mmap()) {
        if(entry.type != STIVALE2_MMAP_USABLE)
            continue;

        if(entry.base > bitmap_size) {
            bitmap = std::span<uint8_t>{(uint8_t*)entry.base + phys_mem_map, bitmap_size};

            auto aligned_bitmap_size = align_up(bitmap_size, 0x1000);
            entry.length -= aligned_bitmap_size;
            entry.base += aligned_bitmap_size;

            for(auto& e : bitmap)
                e = ~0;

            bitmap_constructed = true;
            break;
        }
    }
    ASSERT(bitmap_constructed);

    for(const auto entry : parser.mmap()) {
        if(entry.type != STIVALE2_MMAP_USABLE)
            continue;

        for(size_t i = 0; i < entry.length; i += 0x1000) {
            free_block(entry.base + i);
        }
    }
}

uintptr_t pmm::alloc_block() {
    for(size_t i = 0; i < bitmap.size(); i++) {
        if(bitmap[i] != ~0){
            for(size_t j = 0; j < 8; j++) {
                if(!(bitmap[i] & (1 << j))){
                    // Found free entry
                    bitmap[i] |= (1 << j); // Mark entry as reserved
                    return ((i * 8) + j) * 0x1000;
                }
            }
        }
    }

    return 0;
}

void pmm::free_block(uintptr_t block) {
    auto frame = (block / 0x1000);

    bitmap[frame / 8] &= ~(1 << (frame % 8));
}