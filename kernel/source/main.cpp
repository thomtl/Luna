#include <Luna/common.hpp>

#include <Luna/misc/stivale2.hpp>
#include <Luna/misc/format.hpp>

#include <std/minimal_vector.hpp>

#include <Luna/cpu/cpu.hpp>
#include <Luna/cpu/idt.hpp>

std::minimal_vector<CpuData, 1> per_cpu_data{};

void kernel_main(const stivale2_struct* info) {
    print("Booting Luna, Copyright Thomas Woertman 2020\nBootloader: {:s} {:s}\n", info->bootloader_brand, info->bootloader_version);
    
    auto& cpu_data = per_cpu_data.push_back({});

    cpu_data.set();
    cpu_data.gdt_table.init();

    idt::init_table();
    idt::load();

    asm("int3");

    stivale2::Parser boot_info{info};

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