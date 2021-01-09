#include <Luna/mm/pmm.hpp>
#include <Luna/misc/log.hpp>

#include <Luna/cpu/cpu.hpp>
#include <std/mutex.hpp>

static std::span<uint8_t> bitmap;
static size_t bitmap_size;
static TicketLock pmm_lock{};

void pmm::init(stivale2::Parser& parser) {
    size_t memory_size = 0, highest_page = 0;

    print("pmm: Stivale Memory Map:\n");
    for(auto& entry : parser.mmap()) {
        const auto e = entry;
        print("   - {:#x} - {:#x}: {}\n", e.base, e.base + e.length, stivale2::mmap_type_to_string(e.type));

        if(entry.type == STIVALE2_MMAP_USABLE) {
            memory_size += entry.length;

            auto base = align_up(entry.base, block_size);
            auto size = align_down(entry.length - (base - entry.base), block_size);
            auto top = base + size;

            entry.base = base;
            entry.length = size;

            if(top > highest_page)
                highest_page = top;
        }
    }

    print("pmm: Detected {} MiBs of usable RAM\n", memory_size / 1024 / 1024);

    bitmap_size = (highest_page / block_size) / 8;
    print("pmm: Highest usable address: {:#x} => Bitmap size: {:#x} bytes\n", highest_page, bitmap_size);

    bool bitmap_constructed = false;
    uintptr_t bitmap_base = 0, bitmap_aligned_size = 0;
    for(auto& entry : parser.mmap()) {
        if(entry.type != STIVALE2_MMAP_USABLE)
            continue;

        if(entry.length >= bitmap_size) {
            bitmap = std::span<uint8_t>{(uint8_t*)entry.base + phys_mem_map, bitmap_size};

            bitmap_base = entry.base;
            bitmap_aligned_size = align_up(bitmap_size, pmm::block_size);

            for(auto& e : bitmap)
                e = 0xFF;

            bitmap_constructed = true;
            break;
        }
    }
    ASSERT(bitmap_constructed);

    for(const auto entry : parser.mmap()) {
        if(entry.type != STIVALE2_MMAP_USABLE)
            continue;

        for(size_t i = 0; i < entry.length; i += block_size) {
            free_block(entry.base + i);
        }
    }

    for(uintptr_t addr = bitmap_base; addr < (bitmap_base + bitmap_aligned_size); addr += pmm::block_size)
        reserve_block(addr);
}

uintptr_t pmm::alloc_block() {
    std::lock_guard guard{pmm_lock};

    for(size_t i = 0; i < bitmap.size(); i++) {
        if(bitmap[i] == 0xFF)
            continue;
        
        for(size_t j = 0; j < 8; j++) {
            if((bitmap[i] & (1 << j)) == 0){
                // Found free entry
                bitmap[i] |= (1 << j); // Mark entry as reserved
                return ((i * 8) + j) * block_size;
            }
        }
    }

    return 0;
}

uintptr_t pmm::alloc_n_blocks(size_t n_pages) {
    std::lock_guard guard{pmm_lock};


    size_t left = n_pages;
    for(size_t i = 0; i < bitmap.size(); i++) {
        if(bitmap[i] == 0xFF) {
            left = n_pages;
            continue;
        }

        for(size_t j = 0; j < 8; j++) {
            if((bitmap[i] & (1 << j)) == 0) {
                left--;
                if(left == 0) {
                    j++; // Make sure this page is counted too
                    auto bit_set = [&](size_t bit) { bitmap[bit / 8] |= (1 << (bit % 8)); };

                    auto starting_bit = ((i * 8) + j) - n_pages;
                    for(size_t i = 0; i < n_pages; i++)
                        bit_set(starting_bit + i);

                    return starting_bit * block_size;
                }
            } else {
                left = n_pages;
            }
        }
    }

    return 0;
}

void pmm::free_block(uintptr_t block) {
    std::lock_guard guard{pmm_lock};

    auto frame = (block / block_size);

    bitmap[frame / 8] &= ~(1 << (frame % 8));
}

void pmm::reserve_block(uintptr_t block) {
    std::lock_guard guard{pmm_lock};

    auto frame = (block / block_size);

    bitmap[frame / 8] |= (1 << (frame % 8));
}