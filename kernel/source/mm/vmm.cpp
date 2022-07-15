#include <Luna/mm/vmm.hpp>
#include <Luna/cpu/regs.hpp>

#include <Luna/misc/log.hpp>

#include <std/utility.hpp>

static uint8_t paging_levels;
uintptr_t phys_mem_map;

void vmm::init_bsp(){
    bool la57 = cr4::read() & (1 << 12); // Check if stivale2 enabled 5-Level Paging
    paging_levels = la57 ? 5 : 4;
    phys_mem_map = la57 ? 0xFF00'0000'0000'0000 : 0xFFFF'8000'0000'0000;
    print("vmm: Using {} paging levels with phys mem at {:#x}\n", (uint64_t)paging_levels, phys_mem_map);
}

bool vmm::is_canonical(uintptr_t addr) {
    if(paging_levels == 4) {
        return (addr <= 0x0000'7FFF'FFFF'FFFFull) || (addr >= 0xFFFF'8000'0000'0000ull);
    } else if(paging_levels == 5) {
        return (addr <= 0x00FF'FFFF'FFFF'FFFFull) || (addr >= 0xFF00'0000'0000'0000ull);
    } else {
        PANIC("Unknown paging_levels");
    }
}

paging::Context vmm::create_context(){
    return paging::Context{paging_levels};
}

static constinit std::lazy_initializer<paging::Context> instance;

paging::Context& vmm::get_kernel_context() {
    if(!instance)
        instance.init(paging_levels);

    return *instance;
}