#pragma once

#include <Luna/common.hpp>
#include <Luna/mm/pmm.hpp>

#include <std/utility.hpp>

namespace hmm {
    class Slab {
        public:
        union Entry {
            Entry* next_free;
            uint8_t mem[0];
        };

        static constexpr size_t slab_size = pmm::block_size;

        void init(uintptr_t addr, size_t length, size_t align) {
            _slab = (uint8_t*)addr;
            _free = (Entry*)addr;

            _length = length;
            _align = align;

            auto value_size = align_up(length, align);
            _entry_size = value_size > sizeof(Entry) ? value_size : sizeof(Entry); // Make sure there is at least space for the freelist
            _n_entries = slab_size / _entry_size;

            for(size_t i = 0; i < _n_entries - 1; i++)
                get_entry(i)->next_free = get_entry(i + 1); // _slab[i].next_free = &_slab[i + 1];

            get_entry(_n_entries - 1)->next_free = nullptr;
        }

        uintptr_t alloc(){
            if(!_free)
                return 0; // No free entries left in slab

            auto* free = _free;
            _free = free->next_free;

            return (uintptr_t)free->mem;
        }

        void free(uintptr_t addr) {
            ASSERT(contains(addr));

            auto* entry = (Entry*)addr;

            entry->next_free = _free;
            _free = entry;
        }

        bool contains(uintptr_t addr) {
            auto start = (uintptr_t)_slab;
            auto end = start + slab_size;
            return (addr >= start && addr < end);
        }

        bool is_suitable(uintptr_t length, uintptr_t align) {
            return (length == _length) && (align == _align); // TODO: Some kind of more lenient is_suitable
        }

        private:
        Entry* get_entry(size_t i){
            return (Entry*)(_slab + (i * _entry_size));
        }

        uint8_t* _slab;
        Entry* _free;

        size_t _entry_size, _n_entries;
        uintptr_t _length, _align;
    };

    struct LargeAllocation {
        bool free;
        uintptr_t address;
        size_t size;
    };

    class Allocator {
        struct Pool {
            Pool* next;
            struct PoolItem {
                enum class PoolItemType { None, Slab, LargeAlloc };
                PoolItemType type;
                union {
                    Slab slab;
                    LargeAllocation large_allocation;
                };
            };
            static constexpr size_t n_items = (pmm::block_size - sizeof(Pool*)) / sizeof(PoolItem);
            PoolItem items[n_items];
        };
        static_assert(sizeof(Pool) <= pmm::block_size);

        Pool* alloc_pool();

        Pool* _start;
        public:
        static constexpr size_t large_alloc_threshold = Slab::slab_size / 2; // Use large allocation if there don't fit at least 2 entries in a slab

        Allocator();
        uintptr_t alloc(size_t length, size_t alignment);
        void free(uintptr_t addr);

    };

    void init();
    uintptr_t alloc(size_t length, size_t alignment);
    void free(uintptr_t addr);

    template<typename T, typename... Args>
    T* alloc(Args&&... args) {
        auto* addr = (T*)alloc(sizeof(T), alignof(T));
        if(!addr)
            return nullptr;

        new (addr) T(std::forward<Args>(args)...);
        return addr;
    }
} // namespace hmm


