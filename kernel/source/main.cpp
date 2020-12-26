#include <Luna/common.hpp>

#include <Luna/misc/stivale2.hpp>
#include <Luna/misc/format.hpp>

#include <std/minimal_vector.hpp>

#include <Luna/cpu/cpu.hpp>
#include <Luna/cpu/regs.hpp>
#include <Luna/cpu/idt.hpp>
#include <Luna/cpu/smp.hpp>
#include <Luna/cpu/intel/vmx.hpp>

#include <Luna/mm/pmm.hpp>
#include <Luna/mm/vmm.hpp>
#include <Luna/mm/hmm.hpp>

#include <Luna/drivers/storage/ahci.hpp>
#include <Luna/drivers/iommu.hpp>
#include <Luna/drivers/acpi.hpp>
#include <Luna/drivers/ioapic.hpp>
#include <Luna/drivers/pci.hpp>

#include <Luna/fs/vfs.hpp>

#include <Luna/vmm/vm.hpp>
#include <Luna/vmm/drivers/e9.hpp>
#include <Luna/vmm/drivers/uart.hpp>
#include <Luna/vmm/drivers/cmos.hpp>
#include <Luna/vmm/drivers/fast_a20.hpp>
#include <Luna/vmm/drivers/pci/pci.hpp>
#include <Luna/vmm/drivers/pci/pio_access.hpp>
#include <Luna/vmm/drivers/pci/ecam.hpp>

#include <Luna/vmm/drivers/q35/q35_dram.hpp>

#include <Luna/vmm/drivers/irqs/lapic.hpp>
#include <Luna/vmm/drivers/irqs/8259.hpp>

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

    cpu::early_init();
    vmm::init_bsp(); // This doesn't actually allocate any memory or anything, it just detects 5 level paging and sets phys_mem_map

    stivale2::Parser boot_info{(stivale2_struct*)((uintptr_t)info + phys_mem_map)};
    pmm::init(boot_info);

    auto& cpu_data = allocate_cpu_data();
    cpu_data.gdt_table.init();
    cpu_data.tss_sel = cpu_data.tss_table.load(cpu_data.gdt_table.push_tss(&cpu_data.tss_table));
    cpu_data.set();

    cpu::init();

    simd::init();

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
    msr::write(msr::ia32_pat, msr::pat::default_pat);
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
    asm("sti");

    iommu::init();

    ahci::init();

    vm::init();

    vm::Vm vm{1};
    {
        auto* file = vfs::get_vfs().open("A:/luna/bios.bin");
        ASSERT(file);

        auto bios_size = file->get_size();
        ASSERT((bios_size % 0x1000) == 0);
        
        auto isa_bios_size = min(bios_size, 128 * 1024);
        auto isa_bios_start = 0x10'0000 - isa_bios_size;
        size_t isa_curr = 0;


        uintptr_t map = 0x1'0000'0000 - bios_size;
        for(size_t curr = 0; curr < bios_size; curr += 0x1000) {
            auto block = pmm::alloc_block();
            ASSERT(block);

            if((bios_size - curr) <= isa_bios_size) {
                vm.mm->map(block, isa_bios_start + isa_curr, paging::mapPagePresent | paging::mapPageExecute);
                isa_curr += 0x1000;
            }

            vm.mm->map(block, map + curr, paging::mapPagePresent | paging::mapPageExecute);

            auto* va = (uint8_t*)(block + phys_mem_map);
            ASSERT(file->read(curr, 0x1000, va) == 0x1000);
        }

        // Setup lowmem
        for(size_t i = 0; i < isa_bios_start; i += 0x1000) {
            auto block = pmm::alloc_block();
            ASSERT(block);

            auto* va = (uint8_t*)(block + phys_mem_map);
            memset(va, 0, pmm::block_size);

            vm.mm->map(block, i, paging::mapPagePresent | paging::mapPageWrite | paging::mapPageExecute);
        }
    }

    auto* e9_dev = new vm::e9::Driver{};
    e9_dev->register_pio_driver(&vm);

    auto* cmos_dev = new vm::cmos::Driver{};
    cmos_dev->register_pio_driver(&vm);

    auto* a20_dev = new vm::fast_a20::Driver{};
    a20_dev->register_pio_driver(&vm);

    auto* uart_dev = new vm::uart::Driver{0x3F8};
    uart_dev->register_pio_driver(&vm);

    auto* pci_host_bridge = new vm::pci::HostBridge{};

    auto* pci_pio_access = new vm::pci::pio_access::Driver{vm::pci::pio_access::default_base, 0, pci_host_bridge};
    pci_pio_access->register_pio_driver(&vm);

    auto* pci_mmio_access = new vm::pci::ecam::Driver{0, pci_host_bridge};
    pci_mmio_access->register_mmio_driver(&vm);

    auto* dram_dev = new vm::q35::dram::Driver{&vm, pci_mmio_access};
    dram_dev->register_pci_driver(pci_host_bridge);

    auto* pic_dev = new vm::irqs::pic::Driver{};
    pic_dev->register_pio_driver(&vm);
    
    ASSERT(vm.cpus[0].run());

    print("luna: Done with kernel_main\n");
    while(true)
        ;
}

void kernel_main_ap(stivale2_smp_info* info){
    (void)info; // info is a physical address

    cpu::early_init();
    msr::write(msr::ia32_pat, msr::pat::default_pat);
    vmm::kernel_vmm::get_instance().set();

    auto& cpu_data = allocate_cpu_data();
    cpu_data.gdt_table.init();
    cpu_data.tss_sel = cpu_data.tss_table.load(cpu_data.gdt_table.push_tss(&cpu_data.tss_table));
    cpu_data.set();

    cpu::init();

    cpu_data.lapic.init();

    simd::init();

    idt::load();

    vm::init();

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