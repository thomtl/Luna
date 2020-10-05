#include <Luna/mm/vmm.hpp>
#include <Luna/cpu/regs.hpp>

#include <Luna/misc/format.hpp>

#include <std/utility.hpp>

static uint8_t paging_levels;
uintptr_t phys_mem_map;

void vmm::init_bsp(){
    bool la57 = cr4::read() & (1 << 12); // Check if stivale2 enabled 5-Level Paging
    paging_levels = la57 ? 5 : 4;
    phys_mem_map = la57 ? 0xFF00'0000'0000'0000 : 0xFFFF'8000'0000'0000;
    print("vmm: Using {} paging levels with phys mem at {:#x}\n", (uint64_t)paging_levels, phys_mem_map);

    init_ap();
}

void vmm::init_ap() {
    msr::write(msr::ia32_efer, msr::read(msr::ia32_efer) | (1 << 11)); // Enable No-Execute Support
}

paging::context vmm::create_context(){
    return paging::context{paging_levels};
}

static std::lazy_initializer<paging::context> instance;

paging::context& vmm::kernel_vmm::_instance() {
    if(!instance)
        instance.init(paging_levels);

    return *instance;
}