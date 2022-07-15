#include <Luna/drivers/iommu/intel/sl_paging.hpp>
#include <std/utility.hpp>
#include <std/string.hpp>

#include <Luna/cpu/cpu.hpp>
#include <Luna/cpu/paging.hpp>
#include <Luna/mm/vmm.hpp>

static void delete_table(uintptr_t pa) {
    pmm::free_block(pa);
}

static void clean_table(uintptr_t pa, uint8_t level) {
    auto va = pa + phys_mem_map;
    auto& pml = *(sl_paging::page_table*)va;

    if(level >= 3) {
        for(size_t i = 0; i < 512; i++) {
            if(auto entry = pml[i]; entry.r || entry.w || entry.x) {
                clean_table(entry.frame << 12, level - 1);
            }
        }
    } else {
        for(size_t i = 0; i < 512; i++) {
            if(auto entry = pml[i]; entry.r || entry.w || entry.x) {
                delete_table(entry.frame << 12);
            }
        }
    }
    delete_table(pa);
}

sl_paging::Context::Context(uint8_t levels, bool snoop, bool coherent): levels{levels}, snoop{snoop}, coherent{coherent} {
    ASSERT(levels == 3 || levels == 4 || levels == 5);

    auto pa = create_table();
    root_pa = pa;
}

sl_paging::Context::~Context(){
    clean_table(root_pa, levels);
}

uintptr_t sl_paging::Context::create_table(){
    auto pa = pmm::alloc_block();
    if(!pa)
        PANIC("Couldn't allocate block for sl_paging structures");
    auto va = pa + phys_mem_map;
    vmm::KernelVmm::get_instance().map(pa, va, paging::mapPagePresent | paging::mapPageWrite, msr::pat::wb);

    memset((void*)va, 0, pmm::block_size);
    if(!coherent)
        cpu::cache_flush((void*)va, pmm::block_size);
    return pa;
}

sl_paging::page_entry* sl_paging::Context::walk(uintptr_t iova, bool create_new_tables) {
    auto get_index = [iova](size_t i){ return (iova >> ((9 * (i - 1)) + 12)) & 0x1FF; };
    auto get_level_or_create = [this, create_new_tables](page_table* prev, size_t i) -> page_table* {
        auto* entry = &prev->entries[i];
        if(!entry->r && !entry->w && !entry->x) {
            if(create_new_tables) {
                auto pa = create_table();

                entry->frame = (pa >> 12);
                entry->r = 1;
                entry->w = 1;

                if(!coherent)
                    cpu::cache_flush((void*)entry, sizeof(page_entry));
            } else {
                return nullptr;
            }
        }

        return (page_table*)((entry->frame << 12) + phys_mem_map);
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

void sl_paging::Context::map(uintptr_t pa, uintptr_t iova, uint64_t flags) {
    auto* page = walk(iova, true); // We want to create new tables, so this is guaranteed to return a valid pointer

    if(snoop)
        page->snoop = 1;
    page->frame = (pa >> 12);

    page->r = (flags & paging::mapPagePresent) ? 1 : 0;
    page->w = (flags & paging::mapPageWrite) ? 1 : 0;
    page->x = (flags & paging::mapPageExecute) ? 1 : 0;

    if(!coherent)
        cpu::cache_flush((void*)page, sizeof(page_entry));
}

uintptr_t sl_paging::Context::unmap(uintptr_t iova) {
    auto* entry = walk(iova, false); // Since we're unmapping stuff it wouldn't make sense to make new tables, so we can get null as valid result
    if(!entry)
        return 0; // Page does not exist

    auto old = (entry->frame << 12);

    entry->frame = 0;
    entry->snoop = 0;
    entry->r = 0;
    entry->w = 0;
    entry->x = 0;

    if(!coherent)
        cpu::cache_flush((void*)entry, sizeof(page_entry));

    return old;
}

sl_paging::page_entry sl_paging::Context::get_entry(uintptr_t iova) {
    auto* entry = walk(iova, false);
    if(!entry)
        return {}; // Page does not exist

    return *entry;
}

uintptr_t sl_paging::Context::get_root_pa() const {
    return root_pa;
}