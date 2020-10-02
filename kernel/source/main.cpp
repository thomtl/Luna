#include <Luna/common.hpp>

#include <Luna/misc/stivale2.hpp>
#include <Luna/misc/format.hpp>

#include <std/minimal_vector.hpp>

#include <Luna/cpu/cpu.hpp>
#include <Luna/cpu/idt.hpp>

#include <Luna/mm/pmm.hpp>
#include <Luna/mm/vmm.hpp>

std::minimal_vector<CpuData, 1> per_cpu_data{};

void kernel_main(const stivale2_struct* info) {
    print("Booting Luna, Copyright Thomas Woertman 2020\nBootloader: {:s} {:s}\n", info->bootloader_brand, info->bootloader_version);
    
    vmm::init(); // This doesn't actually allocate any memory or anything, it just detects 5 level paging and sets phys_mem_map

    stivale2::Parser boot_info{(stivale2_struct*)((uintptr_t)info + phys_mem_map)};
    pmm::init(boot_info);

    auto& cpu_data = per_cpu_data.push_back({});
    cpu_data.set();
    cpu_data.gdt_table.init();

    idt::init_table();
    idt::load();

    auto& kernel_vmm = vmm::kernel_vmm::get_instance();
    for(size_t i = 0; i < 0x8000'0000; i += pmm::block_size)
        kernel_vmm.map(i, i + kernel_vbase, paging::mapPagePresent | paging::mapPageWrite | paging::mapPageExecute);

    // Map Usable, Kernel, Modules, and Bootloader memory, MMIO and ACPI memory will be mapped by the drivers themselves
    for(const auto entry : boot_info.mmap())
        if(entry.type == STIVALE2_MMAP_USABLE || entry.type == STIVALE2_MMAP_KERNEL_AND_MODULES || entry.type == STIVALE2_MMAP_BOOTLOADER_RECLAIMABLE)
            for(size_t i = entry.base; i < (entry.base + entry.length); i += pmm::block_size)
                kernel_vmm.map(i, i + phys_mem_map, paging::mapPagePresent | paging::mapPageWrite);
    kernel_vmm.set();
    print("vmm: Set kernel page tables\n");

    while(true)
        ;
}

constexpr size_t bsp_stack_size = 0x1000;
uint8_t bsp_stack[bsp_stack_size];

stivale2_header_tag_smp smp = {
    .tag = {.identifier = STIVALE2_HEADER_TAG_SMP_ID, .next = 0},
    .flags = 1, // Use x2APIC
};

stivale2_tag la57 = {
    .identifier = STIVALE2_HEADER_TAG_5LV_PAGING_ID,
    .next = (uint64_t)&smp
};

[[gnu::section(".stivale2hdr")]]
stivale2_header header = {
    .entry_point = (uint64_t)kernel_main,
    .stack = (uint64_t)(bsp_stack + bsp_stack_size),
    .flags = 0, // No KASLR
    .tags = (uint64_t)&la57
};