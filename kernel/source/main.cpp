#include <Luna/common.hpp>

#include <Luna/misc/stivale2.hpp>
#include <Luna/misc/format.hpp>

#include <std/minimal_vector.hpp>

#include <Luna/cpu/cpu.hpp>
#include <Luna/cpu/idt.hpp>
#include <Luna/cpu/smp.hpp>

#include <Luna/mm/pmm.hpp>
#include <Luna/mm/vmm.hpp>
#include <Luna/mm/hmm.hpp>

#include <Luna/drivers/iommu.hpp>
#include <Luna/drivers/acpi.hpp>
#include <Luna/drivers/ioapic.hpp>
#include <Luna/drivers/pci.hpp>

#include <Luna/drivers/storage/ahci.hpp>

#include <std/mutex.hpp>

std::minimal_vector<CpuData, 1> per_cpu_data{};

static CpuData& allocate_cpu_data() {
    static TicketLock lock{};
    std::lock_guard guard{lock};

    return per_cpu_data.push_back({});
}

void kernel_main_ap(stivale2_smp_info* info);

void kernel_main(const stivale2_struct* info) {
    print("Booting Luna, Copyright Thomas Woertman 2020\nBootloader: {:s} {:s}\n", info->bootloader_brand, info->bootloader_version);
    
    vmm::init_bsp(); // This doesn't actually allocate any memory or anything, it just detects 5 level paging and sets phys_mem_map

    stivale2::Parser boot_info{(stivale2_struct*)((uintptr_t)info + phys_mem_map)};
    pmm::init(boot_info);

    auto& cpu_data = allocate_cpu_data();
    cpu_data.gdt_table.init();
    cpu_data.set();

    idt::init_table();
    idt::load();

    auto& kernel_vmm = vmm::kernel_vmm::get_instance();
    for(size_t i = 0; i < 0x8000'0000; i += pmm::block_size)
        kernel_vmm.map(i, i + kernel_vbase, paging::mapPagePresent | paging::mapPageWrite | paging::mapPageExecute);

    for(size_t i = 0; i < 0xFFFF'FFFF; i += pmm::block_size)
            kernel_vmm.map(i, i + phys_mem_map, paging::mapPagePresent | paging::mapPageWrite);

    // Map Usable, Kernel, Modules, and Bootloader memory, MMIO and ACPI memory will be mapped by the drivers themselves
    for(const auto entry : boot_info.mmap())
        for(size_t i = entry.base; i < (entry.base + entry.length); i += pmm::block_size)
            kernel_vmm.map(i, i + phys_mem_map, paging::mapPagePresent | paging::mapPageWrite);
    kernel_vmm.set();
    print("vmm: Set kernel page tables\n");

    hmm::init();
    print("hmm: Initialized SLAB allocator\n");

    cpu_data.lapic.init();

    acpi::init(boot_info);
    ioapic::init();
    acpi::init_sci();

    smp::start_cpus(boot_info, kernel_main_ap);

    pci::init();
    iommu::init();
    asm("sti");

    ahci::Controller c{pci::device_by_class(1, 6, 1)};
    (void)c;

    print("luna: Done with kernel_main\n");
    while(true)
        ;
}

void kernel_main_ap(stivale2_smp_info* info){
    (void)info;

    vmm::init_ap();
    vmm::kernel_vmm::get_instance().set();

    auto& cpu_data = allocate_cpu_data();
    cpu_data.gdt_table.init();
    cpu_data.set();
    cpu_data.lapic.init();

    idt::load();

    asm("sti");
    while(1)
        ;
}

constexpr size_t bsp_stack_size = 0x1000;
uint8_t bsp_stack[bsp_stack_size];

stivale2_header_tag_smp smp_tag = {
    .tag = {.identifier = STIVALE2_HEADER_TAG_SMP_ID, .next = 0},
    .flags = 1, // Use x2APIC
};

stivale2_tag la57_tag = {
    .identifier = STIVALE2_HEADER_TAG_5LV_PAGING_ID,
    .next = (uint64_t)&smp_tag
};

[[gnu::section(".stivale2hdr")]]
stivale2_header header = {
    .entry_point = (uint64_t)kernel_main,
    .stack = (uint64_t)(bsp_stack + bsp_stack_size),
    .flags = 0, // No KASLR
    .tags = (uint64_t)&la57_tag
};