#include <Luna/cpu/paging.hpp>
#include <std/utility.hpp>
#include <std/string.hpp>

static std::pair<uintptr_t, uintptr_t> create_table(){
    auto pa = pmm::alloc_block();
    if(!pa)
        PANIC("Couldn't allocate block for paging structures");
    auto va = pa + phys_mem_map;

    memset((void*)va, 0, pmm::block_size);
    return {pa, va};
}

static void delete_table(uintptr_t pa) {
    pmm::free_block(pa);
}

static void clean_table(uintptr_t pa, uint8_t level) {
    auto va = pa + phys_mem_map;
    auto& pml = *(paging::page_table*)va;

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

paging::context::context(uint8_t levels): levels{levels} {
    ASSERT(levels == 4 || levels == 5);

    const auto [pa, _] = create_table();
    root_pa = pa;
}

paging::context::~context(){
    clean_table(root_pa, levels);
}

paging::page_entry* paging::context::walk(uintptr_t va, bool create_new_tables) {
    auto get_index = [va](size_t i){ return (va >> ((9 * (i - 1)) + 12)) & 0x1FF; };
    auto get_level_or_create = [create_new_tables](page_table* prev, size_t i) -> page_table* {
        auto& entry = (*prev)[i];
        if(!entry.present) {
            if(create_new_tables) {
                const auto [pa, _] = create_table();

                entry.frame = (pa >> 12);
                entry.present = 1;
                entry.writeable = 1;
                entry.user = 1;
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

void paging::context::map(uintptr_t pa, uintptr_t va, uint64_t flags, uint64_t cache) {
    auto& page = *walk(va, true); // We want to create new tables, so this is guaranteed to return a valid pointer

    page.present = (flags & mapPagePresent) ? 1 : 0;
    page.writeable = (flags & mapPageWrite) ? 1 : 0;
    page.user = (flags & mapPageUser) ? 1 : 0;
    page.no_execute = (flags & mapPageExecute) ? 0 : 1;
    page.frame = (pa >> 12);

    // TODO: Use PAT
    if(cache == cacheDisable)
        page.cache_disable = 1;
    else if(cache == cacheWritethrough)
        page.writethrough = 1;
    // Writeback is default so don't do anything

    asm volatile("invlpg (%0)" : : "r"(va) : "memory");
}

uintptr_t paging::context::unmap(uintptr_t va) {
    auto* entry = walk(va, false); // Since we're unmapping stuff it wouldn't make sense to make new tables, so we can get null as valid result
    if(!entry)
        return 0; // Page does not exist

    uintptr_t ret = (entry->frame << 12);
    entry->present = 0;
    entry->writeable = 0;
    entry->no_execute = 1;

    entry->cache_disable = 0;
    entry->writethrough = 0;

    entry->user = 0;
    entry->frame = 0;

    asm volatile("invlpg (%0)" : : "r"(va) : "memory");

    return ret;
}

uintptr_t paging::context::get_phys(uintptr_t va) {
    auto* entry = walk(va, false); // Since we're just getting stuff it wouldn't make sense to make new tables, so we can get null as valid result
    if(!entry)
        return 0; // Page does not exist

    return (entry->frame << 12);
}

uintptr_t paging::context::get_root_pa() const {
    return root_pa;
}

void paging::context::set() const {
    asm volatile("mov %0, %%cr3" : : "r"(get_root_pa()) : "memory");
}