#include <Luna/cpu/intel/ept.hpp>
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
    auto& pml = *(ept::page_table*)va;

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

ept::context::context(uint8_t levels): levels{levels} {
    ASSERT(levels == 4 || levels == 5);

    const auto [pa, _] = create_table();
    root_pa = pa;
}

ept::context::~context(){
    clean_table(root_pa, levels);
}

ept::page_entry* ept::context::walk(uintptr_t va, bool create_new_tables) {
    auto get_index = [va](size_t i){ return (va >> ((9 * (i - 1)) + 12)) & 0x1FF; };
    auto get_level_or_create = [create_new_tables](page_table* prev, size_t i) -> page_table* {
        auto& entry = (*prev)[i];
        if(!entry.r) {
            if(create_new_tables) {
                const auto [pa, _] = create_table();

                entry.frame = (pa >> 12);
                entry.r = 1;
                entry.w = 1;
                entry.x = 1;
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

void ept::context::map(uintptr_t pa, uintptr_t va, uint64_t flags) {
    auto& page = *walk(va, true); // We want to create new tables, so this is guaranteed to return a valid pointer

    page.r = (flags & paging::mapPagePresent) ? 1 : 0;
    page.w = (flags & paging::mapPageWrite) ? 1 : 0;
    page.x = (flags & paging::mapPageExecute) ? 1 : 0;
    page.mem_type = msr::pat::write_back;
    page.frame = (pa >> 12);

    invept();
}

void ept::context::protect(uintptr_t va, uint64_t flags) {
    auto* page = walk(va, false);
    if(!page)
        return;

    page->r = (flags & paging::mapPagePresent) ? 1 : 0;
    page->w = (flags & paging::mapPageWrite) ? 1 : 0;
    page->x = (flags & paging::mapPageExecute) ? 1 : 0;

    invept();
}

uintptr_t ept::context::unmap(uintptr_t va) {
    auto* entry = walk(va, false); // Since we're unmapping stuff it wouldn't make sense to make new tables, so we can get null as valid result
    if(!entry)
        return 0; // Page does not exist

    uintptr_t ret = (entry->frame << 12);
    entry->r = 0;
    entry->w = 0;
    entry->x = 1;
    entry->frame = 0;

    invept();

    return ret;
}

uintptr_t ept::context::get_phys(uintptr_t va) {
    auto off = va & 0xFFF;
    auto* entry = walk(va, false); // Since we're just getting stuff it wouldn't make sense to make new tables, so we can get null as valid result
    if(!entry)
        return 0; // Page does not exist

    return (entry->frame << 12) + off;
}

uintptr_t ept::context::get_root_pa() const {
    return root_pa;
}

void ept::context::invept() {
    // invept mode 1 was already guaranteed to be supported by vmx::init()
    struct {
        uint64_t eptp;
        uint64_t reserved;
    } descriptor{root_pa, 0};
    uint64_t mode = 1; // Single Context
    
    asm volatile("invept %1, %0" : : "r"(mode), "m"(descriptor) : "memory");
}