#include <Luna/drivers/intel/sl_paging.hpp>
#include <std/utility.hpp>
#include <std/string.hpp>

#include <Luna/cpu/paging.hpp>
#include <Luna/mm/vmm.hpp>

static void delete_table(uintptr_t pa) {
    pmm::free_block(pa);
}

static void clean_table(uintptr_t pa, uint8_t level) {
    auto va = pa + phys_mem_map;
    auto& pml = *(sl_paging::page_table*)va;

    if(level >= 3) {
        for(size_t i = 0; i < 512; i++)
            if(pml[i].r)
                clean_table(pml[i].frame << 12, level - 1);
    } else {
        for(size_t i = 0; i < 512; i++)
            if(pml[i].r)
                delete_table(pml[i].frame << 12);
    }
    delete_table(pa);
}

sl_paging::context::context(uint8_t levels, uint64_t cache_mode): levels{levels}, cache_mode{cache_mode} {
    ASSERT(levels == 3 || levels == 4 || levels == 5);

    auto pa = create_table();
    root_pa = pa;
}

sl_paging::context::~context(){
    clean_table(root_pa, levels);
}

uintptr_t sl_paging::context::create_table(){
    auto pa = pmm::alloc_block();
    if(!pa)
        PANIC("Couldn't allocate block for sl_paging structures");
    auto va = pa + phys_mem_map;
    vmm::kernel_vmm::get_instance().map(pa, va, paging::mapPagePresent | paging::mapPageWrite, cache_mode);

    memset((void*)va, 0, pmm::block_size);
    return pa;
}

sl_paging::page_entry* sl_paging::context::walk(uintptr_t iova, bool create_new_tables) {
    auto get_index = [iova](size_t i){ return (iova >> ((9 * (i - 1)) + 12)) & 0x1FF; };
    auto get_level_or_create = [this, create_new_tables](page_table* prev, size_t i) -> page_table* {
        auto& entry = (*prev)[i];
        if(!entry.r) {
            if(create_new_tables) {
                auto pa = create_table();

                entry.frame = (pa >> 12);
                entry.r = 1;
                entry.w = 1;
            } else {
                return nullptr;
            }
        }

        return (page_table*)((entry.frame << 12) + phys_mem_map);
    };

    auto* curr = (page_table*)(root_pa + phys_mem_map);
    for(size_t i = levels; i >= 2; i--) {
        curr = get_level_or_create(curr, get_index(i));
        if(!curr)
            return nullptr;
    }

    auto& pml1 = *curr;
    return &pml1[get_index(1)];
}

void sl_paging::context::map(uintptr_t pa, uintptr_t iova, uint64_t flags) {
    auto& page = *walk(iova, true); // We want to create new tables, so this is guaranteed to return a valid pointer

    page.r = (flags & paging::mapPagePresent) ? 1 : 0;
    page.w = (flags & paging::mapPageWrite) ? 1 : 0;
    page.x = (flags & paging::mapPageExecute) ? 1 : 0;
    page.frame = (pa >> 12);
}

uintptr_t sl_paging::context::unmap(uintptr_t iova) {
    auto* entry = walk(iova, false); // Since we're unmapping stuff it wouldn't make sense to make new tables, so we can get null as valid result
    if(!entry)
        return 0; // Page does not exist

    entry->r = 0;
    entry->w = 0;
    entry->x = 0;

    auto pa = (entry->frame << 12);
    entry->frame = 0;

    return pa;
}

uintptr_t sl_paging::context::get_root_pa() const {
    return root_pa;
}