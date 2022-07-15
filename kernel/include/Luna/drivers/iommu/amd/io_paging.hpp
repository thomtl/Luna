#pragma once

#include <Luna/common.hpp>
#include <Luna/mm/pmm.hpp>

namespace io_paging
{
    struct [[gnu::packed]] page_entry {
        uint64_t present : 1;
        uint64_t reserved : 4;
        uint64_t accessed : 1;
        uint64_t dirty : 1;
        uint64_t ignored : 2;
        uint64_t next_level : 3;
        uint64_t frame : 40;
        uint64_t reserved_0 : 7;
        uint64_t u : 1;
        uint64_t coherent : 1;
        uint64_t r : 1;
        uint64_t w : 1;
        uint64_t ignored_0 : 1;
    };
    static_assert(sizeof(page_entry) == sizeof(uint64_t));

    struct [[gnu::packed]] page_table {
        page_entry entries[512];

        page_entry& operator[](size_t i){
            return entries[i];
        }
    };
    static_assert(sizeof(page_table) == pmm::block_size);

    class Context {
        public:
        Context(uint8_t levels);
        ~Context();

        void map(uintptr_t pa, uintptr_t iova, uint64_t flags);
        uintptr_t unmap(uintptr_t iova);
        page_entry get_page(uintptr_t iova);

        uintptr_t get_root_pa() const;

        private:
        page_entry* walk(uintptr_t iova, bool create_new_tables);

        uint8_t levels;
        uintptr_t root_pa;
    };
} // namespace io_paging
