#include <Luna/mm/hmm.hpp>
#include <Luna/mm/vmm.hpp>

#include <Luna/cpu/cpu.hpp>

#include <std/string.hpp>
#include <std/mutex.hpp>

#include <Luna/misc/format.hpp>


static uintptr_t heap_loc = 0xFFFF'FFFE'0000'0000; // TODO: Move this below kernel_vbase sometime?
static uintptr_t allocate_page(){
    auto pa = pmm::alloc_block();
    auto va = heap_loc;
    heap_loc += pmm::block_size;

    vmm::kernel_vmm::get_instance().map(pa, va, paging::mapPagePresent | paging::mapPageWrite);
    memset((void*)va, 0, pmm::block_size);

    return va;
}

hmm::Allocator::Allocator() {
    _start = alloc_pool();
}

uintptr_t hmm::Allocator::alloc(size_t length, size_t alignment) {
    auto effective_size = align_up(length, alignment);
    if(effective_size > large_alloc_threshold) {
        auto n_pages = div_ceil(effective_size, pmm::block_size);

        for(auto* curr = _start; curr; curr = curr->next) {
            for(size_t i = 0; i < curr->n_items; i++) {
                if(curr->items[i].type == Pool::PoolItem::PoolItemType::LargeAlloc) {
                    if(curr->items[i].large_allocation.free && curr->items[i].large_allocation.size >= n_pages) { // First fit
                        curr->items[i].large_allocation.free = false;
                        return curr->items[i].large_allocation.address;
                    }
                }
            }
        }

        // There's no existing LargeAlloc for us, so make a new one, there is only free space in the last pool entry
        auto* last = _start;
        for(; last->next; last = last->next);

        auto create_new_large_alloc = [this, n_pages, length, alignment, effective_size](Pool* pool) -> uintptr_t {
            for(size_t i = 0; i < pool->n_items; i++) {
                if(pool->items[i].type == Pool::PoolItem::PoolItemType::None) {
                    pool->items[i].type = Pool::PoolItem::PoolItemType::LargeAlloc;

                    // allocate_page() is a bump allocator so this is all fine and dandy
                    auto addr = allocate_page();
                    for(size_t i = 0; i < n_pages - 1; i++)
                        allocate_page();
                    
                    pool->items[i].large_allocation = LargeAllocation{.free = false, .address = addr, .size = effective_size};
                    return addr;
                }
            }

            return 0;
        };

        if(auto ret = create_new_large_alloc(last); ret)
            return ret;

        // No space for a new entry in the last pool entry, so make a new pool entry and try again
        auto* new_pool = alloc_pool();
        last->next = new_pool;
    
        // And try again with our new pool
        if(auto ret = create_new_large_alloc(new_pool); ret)
            return ret;

        PANIC("Was not able to create a new LargeAlloc even after creating a new pool");
    } else {
        for(auto* curr = _start; curr; curr = curr->next)
            for(size_t i = 0; i < curr->n_items; i++)
                if(curr->items[i].type == Pool::PoolItem::PoolItemType::Slab)
                    if(curr->items[i].slab.is_suitable(length, alignment))
                        return curr->items[i].slab.alloc();


        // There's no existing SLAB for us, so make a new one, there is only free space in the last pool entry
        auto* last = _start;
        for(; last->next; last = last->next);
    
        auto create_new_slab_and_alloc = [this, length, alignment](Pool* pool) -> uintptr_t {
            for(size_t i = 0; i < pool->n_items; i++) {
                if(pool->items[i].type == Pool::PoolItem::PoolItemType::None) {
                    pool->items[i].type = Pool::PoolItem::PoolItemType::Slab;
                    pool->items[i].slab = {};
                    pool->items[i].slab.init(allocate_page(), length, alignment);

                    // Since we've just made a new SLAB we can just allocate from this one
                    return pool->items[i].slab.alloc();
                }
            }

            return 0;
        };

        if(auto ret = create_new_slab_and_alloc(last); ret)
            return ret;
    
        // No space for a new entry in the last pool entry, so make a new pool entry and try again
        auto* new_pool = alloc_pool();
        last->next = new_pool;
    
        // And try again with our new pool
        if(auto ret = create_new_slab_and_alloc(new_pool); ret)
            return ret;

        PANIC("Was not able to create a new SLAB even after creating a new pool");
    }    
}

void hmm::Allocator::free(uintptr_t addr) {
    if(!addr)
        return;
    
    for(auto* curr = _start; curr; curr = curr->next) {
        for(size_t i = 0; i < curr->n_items; i++) {
            if(curr->items[i].type == Pool::PoolItem::PoolItemType::Slab) {
                if(curr->items[i].slab.contains(addr))
                    return curr->items[i].slab.free(addr);
            } else if(curr->items[i].type == Pool::PoolItem::PoolItemType::LargeAlloc) {
                if(curr->items[i].large_allocation.address == addr) {
                    ASSERT(!curr->items[i].large_allocation.free);
                    curr->items[i].large_allocation.free = true;
                    return;
                }
            }
        }
    }

    PANIC("Was not able to find SLAB or LargeAlloc corresponding to address");
}

uintptr_t hmm::Allocator::realloc(uintptr_t old, size_t size, size_t alignment){
    if(old == 0 && size == 0)
        return 0;
    
    if(size == 0) {
        free(old);
        return 0;
    }

    if(old == 0)
        return alloc(size, alignment);

    size_t old_size = 0;
    bool found_old_size = false;
    for(auto* curr = _start; curr; curr = curr->next) {
        for(size_t i = 0; i < curr->n_items; i++) {
            if(curr->items[i].type == Pool::PoolItem::PoolItemType::Slab) {
                if(curr->items[i].slab.contains(old)) {
                    old_size = curr->items[i].slab.entry_size();
                    found_old_size = true;
                    goto end;
                }
            } else if(curr->items[i].type == Pool::PoolItem::PoolItemType::LargeAlloc) {
                if(curr->items[i].large_allocation.address == old) {
                    ASSERT(!curr->items[i].large_allocation.free);
                    
                    old_size = curr->items[i].large_allocation.size;
                    found_old_size = true;
                    goto end;
                }
            }
        }
    }
    end:
    ASSERT(found_old_size);

    auto ret = alloc(size, alignment);
    if(ret) {
        memcpy((void*)ret, (void*)old, old_size);
        free(old);
    }

    return ret;
}

hmm::Allocator::Pool* hmm::Allocator::alloc_pool(){
    auto* pool = (Pool*)allocate_page();
    for(size_t i = 0; i < pool->n_items; i++)
        pool->items[i].type = Pool::PoolItem::PoolItemType::None;

    return pool;
}


static std::lazy_initializer<hmm::Allocator> global_allocator;
static TicketLock global_allocator_lock{};

void hmm::init() {
    std::lock_guard guard{global_allocator_lock};

    global_allocator.init();
}

uintptr_t hmm::alloc(size_t length, size_t alignment) {
    std::lock_guard guard{global_allocator_lock};

    return global_allocator->alloc(length, alignment);
}

uintptr_t hmm::realloc(uintptr_t old, size_t size, size_t alignment) {
    std::lock_guard guard{global_allocator_lock};

    return global_allocator->realloc(old, size, alignment);
}

void hmm::free(uintptr_t addr) {
    std::lock_guard guard{global_allocator_lock};

    global_allocator->free(addr);
}