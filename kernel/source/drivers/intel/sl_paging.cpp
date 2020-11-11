#include <Luna/drivers/intel/sl_paging.hpp>
#include <std/utility.hpp>
#include <std/string.hpp>

#include <Luna/cpu/paging.hpp>

static std::pair<uintptr_t, uintptr_t> create_table(){
    auto pa = pmm::alloc_block();
    if(!pa)
        PANIC("Couldn't allocate block for sl_paging structures");
    auto va = pa + phys_mem_map;

    memset((void*)va, 0, pmm::block_size);
    return {pa, va};
}

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

sl_paging::context::context(uint8_t levels): levels{levels} {
    ASSERT(levels == 3 || levels == 4 || levels == 5);

    const auto [pa, _] = create_table();
    root_pa = pa;
}

sl_paging::context::~context(){
    clean_table(root_pa, levels);
}

void sl_paging::context::map(uintptr_t pa, uintptr_t iova, uint64_t flags) {
    auto get_index = [iova](size_t i){ return (iova >> ((9 * (i - 1)) + 12)) & 0x1FF; };
    auto get_level_or_create = [](page_table* prev, size_t i) -> page_table* {
        auto& entry = (*prev)[i];
        if(!entry.r) {
            const auto [pa, _] = create_table();

            entry.frame = (pa >> 12);
            entry.r = 1;
            entry.w = 1;
        }

        return (page_table*)((entry.frame << 12) + phys_mem_map);
    };

    auto* curr = (page_table*)(root_pa + phys_mem_map);
    if(levels >= 5)
        curr = get_level_or_create(curr, get_index(5));
    if(levels >= 4)
        curr = get_level_or_create(curr, get_index(4));
    curr = get_level_or_create(curr, get_index(3));
    curr = get_level_or_create(curr, get_index(2));

    auto& pml1 = *curr;
    auto& pml1_e = pml1[get_index(1)];

    pml1_e.r = (flags & paging::mapPagePresent) ? 1 : 0;
    pml1_e.w = (flags & paging::mapPageWrite) ? 1 : 0;
    pml1_e.x = (flags & paging::mapPageExecute) ? 1 : 0;
    pml1_e.frame = (pa >> 12);
}

// TODO: Maybe merge table walking code with map?
uintptr_t sl_paging::context::unmap(uintptr_t iova) {
    auto get_index = [iova](size_t i){ return (iova >> ((9 * (i - 1)) + 12)) & 0x1FF; };
    auto get_level = [](page_table* prev, size_t i) -> page_table* {
        auto& entry = (*prev)[i];
        if(!entry.r)
            return nullptr;

        return (page_table*)((entry.frame << 12) + phys_mem_map);
    };

    auto* curr = (page_table*)(root_pa + phys_mem_map);
    if(levels >= 5) {
        curr = get_level(curr, get_index(5));
        if(!curr)
            return 0;
    }

    if(levels >= 4) {
        curr = get_level(curr, get_index(4));
        if(!curr)
            return 0;
    }

    curr = get_level(curr, get_index(3));
    if(!curr)
        return 0;

    curr = get_level(curr, get_index(2));
    if(!curr)
        return 0;

    auto& pml1 = *curr;
    auto& pml1_e = pml1[get_index(1)];

    pml1_e.r = 0;
    pml1_e.w = 0;
    pml1_e.x = 0;

    auto pa = (pml1_e.frame << 12);
    pml1_e.frame = 0;

    return pa;
}

uintptr_t sl_paging::context::get_root_pa() const {
    return root_pa;
}