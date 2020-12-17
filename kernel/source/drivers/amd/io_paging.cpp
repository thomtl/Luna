#include <Luna/drivers/amd/io_paging.hpp>
#include <std/utility.hpp>
#include <std/string.hpp>

#include <Luna/cpu/paging.hpp>

static std::pair<uintptr_t, uintptr_t> create_table(){
    auto pa = pmm::alloc_block();
    if(!pa)
        PANIC("Couldn't allocate block for io_paging structures");
    auto va = pa + phys_mem_map;

    memset((void*)va, 0, pmm::block_size);
    return {pa, va};
}

static void delete_table(uintptr_t pa) {
    pmm::free_block(pa);
}

static void clean_table(uintptr_t pa, uint8_t level) {
    auto va = pa + phys_mem_map;
    auto& pml = *(io_paging::page_table*)va;

    if(level >= 3) {
        for(size_t i = 0; i < 512; i++)
            if(pml[i].present)
                clean_table(pml[i].frame << 12, level - 1);
    } else {
        for(size_t i = 0; i < 512; i++)
            if(pml[i].present)
                delete_table(pml[i].frame << 12);
    }
    delete_table(pa);
}

io_paging::context::context(uint8_t levels): levels{levels} {
    const auto [pa, _] = create_table();
    root_pa = pa;
}

io_paging::context::~context(){
    clean_table(root_pa, levels);
}

io_paging::page_entry* io_paging::context::walk(uintptr_t iova, bool create_new_tables) {
    auto get_index = [iova](size_t i){ return (iova >> ((9 * (i - 1)) + 12)) & 0x1FF; };
    auto get_level_or_create = [create_new_tables](page_table* prev, size_t i, size_t level) -> page_table* {
        auto& entry = (*prev)[i];
        if(!entry.present) {
            if(create_new_tables) {
                const auto [pa, _] = create_table();

                entry.frame = (pa >> 12);
                entry.present = 1;
                entry.r = 1;
                entry.w = 1;

                entry.next_level = (level - 1);
            } else {
                return nullptr;
            }
        }

        return (page_table*)((entry.frame << 12) + phys_mem_map);
    };

    auto* curr = (page_table*)(root_pa + phys_mem_map);
    for(size_t i = levels; i >= 2; i--) {
        curr = get_level_or_create(curr, get_index(i), i);
        if(!curr)
            return nullptr;
    }

    auto& pml1 = *curr;
    return &pml1[get_index(1)];
}

void io_paging::context::map(uintptr_t pa, uintptr_t iova, uint64_t flags) {
    auto& page = *walk(iova, true); // We want to create new tables, so this is guaranteed to return a valid pointer

    page.present = 1;
    page.r = (flags & paging::mapPagePresent) ? 1 : 0;
    page.w = (flags & paging::mapPageWrite) ? 1 : 0;
    page.next_level = 0;
    page.coherent = 1;
    page.frame = (pa >> 12);
}

uintptr_t io_paging::context::unmap(uintptr_t iova) {
    auto* entry = walk(iova, false); // Since we're unmapping stuff it wouldn't make sense to make new tables, so we can get null as valid result
    if(!entry)
        return 0; // Page does not exist
    
    entry->present = 0;
    entry->r = 0;
    entry->w = 0;

    auto pa = (entry->frame << 12);
    entry->frame = 0;

    return pa;
}

io_paging::page_entry io_paging::context::get_page(uintptr_t iova) {
    auto* entry = walk(iova, false);
    if(!entry)
        return {};

    return *entry;
}

uintptr_t io_paging::context::get_root_pa() const {
    return root_pa;
}