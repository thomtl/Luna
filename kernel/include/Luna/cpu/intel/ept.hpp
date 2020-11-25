#pragma once

#include <Luna/common.hpp>
#include <Luna/mm/pmm.hpp>

namespace ept {
    struct [[gnu::packed]] page_entry {
        uint64_t r : 1;
        uint64_t w : 1;
        uint64_t x : 1;
        uint64_t mem_type : 3;
        uint64_t ignore_pat : 1;
        uint64_t ignored : 1;
        uint64_t accessed : 1;
        uint64_t dirty : 1;
        uint64_t linear_x : 1;
        uint64_t ignored_0 : 1;
        uint64_t frame : 40;
        uint64_t ignored_1 : 8;
        uint64_t super_visor_shadow : 1;
        uint64_t spp : 1;
        uint64_t ignored_2 : 1;
        uint64_t suppress_ve : 1;
    };
    static_assert(sizeof(page_entry) == sizeof(uint64_t));

    struct [[gnu::packed]] page_table {
        page_entry entries[512];

        page_entry& operator[](size_t i){
            return entries[i];
        }
    };
    static_assert(sizeof(page_table) == pmm::block_size);

    class context {
        public:
        context(uint8_t levels);
        ~context();

        void map(uintptr_t pa, uintptr_t va, uint64_t flags);
        uintptr_t unmap(uintptr_t va);
        uintptr_t get_phys(uintptr_t va);

        uintptr_t get_root_pa() const;

        uint8_t get_levels() const { return levels; }

        private:
        page_entry* walk(uintptr_t va, bool create_new_tables);

        uint8_t levels;
        uintptr_t root_pa;
    };
} // namespace ept
