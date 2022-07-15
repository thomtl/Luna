#pragma once

#include <Luna/common.hpp>
#include <Luna/mm/pmm.hpp>
#include <Luna/cpu/regs.hpp>

namespace paging
{
    struct [[gnu::packed]] page_entry {
        uint64_t present : 1;
        uint64_t writeable : 1;
        uint64_t user : 1;
        uint64_t writethrough : 1;
        uint64_t cache_disable : 1;
        uint64_t accessed : 1;
        uint64_t dirty : 1;
        uint64_t pat : 1;
        uint64_t global : 1;
        uint64_t available0 : 3;
        uint64_t frame : 40;
        uint64_t available1 : 7;
        uint64_t pke : 4;
        uint64_t no_execute : 1;
    };
    static_assert(sizeof(page_entry) == sizeof(uint64_t));

    struct [[gnu::packed]] page_table {
        page_entry entries[512];

        page_entry& operator[](size_t i){
            return entries[i];
        }
    };
    static_assert(sizeof(page_table) == pmm::block_size);

    enum {
        mapPagePresent = (1 << 0),
        mapPageWrite = (1 << 1),
        mapPageExecute = (1 << 2),
        mapPageUser = (1 << 3),
    };

    class Context {
        public:
        Context(uint8_t levels);
        ~Context();

        void map(uintptr_t pa, uintptr_t va, uint64_t flags, uint8_t caching = msr::pat::write_back);
        void set_caching(uintptr_t va, uint8_t caching);
        uintptr_t unmap(uintptr_t va);
        uintptr_t get_phys(uintptr_t va);
        page_entry get_page(uintptr_t va);

        uintptr_t get_root_pa() const;
        void set() const;

        private:
        page_entry* walk(uintptr_t va, bool create_new_tables);

        uint8_t levels;
        uintptr_t root_pa;
    };
} // namespace paging
