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

void paging::context::map(uintptr_t pa, uintptr_t va, uint64_t flags) {
    auto get_index = [va](size_t i){ return (va >> ((9 * (i - 1)) + 12)) & 0x1FF; };
    auto get_level_or_create = [](page_table* prev, size_t i) -> page_table* {
        auto& entry = (*prev)[i];
        if(!entry.present) {
            const auto [pa, _] = create_table();

            entry.frame = (pa >> 12);
            entry.present = 1;
            entry.writeable = 1;
            entry.user = 1;
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

    pml1_e.present = (flags & mapPagePresent) ? 1 : 0;
    pml1_e.writeable = (flags & mapPageWrite) ? 1 : 0;
    pml1_e.user = (flags & mapPageUser) ? 1 : 0;
    pml1_e.no_execute = (flags & mapPageExecute) ? 0 : 1;
    pml1_e.frame = (pa >> 12);

    asm volatile("invlpg (%0)" : : "r"(va) : "memory"); 
}

uintptr_t paging::context::get_root_pa() const {
    return root_pa;
}

void paging::context::set() const {
    asm volatile("mov %0, %%cr3" : : "r"(get_root_pa()) : "memory");
}