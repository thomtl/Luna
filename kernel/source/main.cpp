#include <Luna/common.hpp>

#include <Luna/misc/stivale2.hpp>
#include <Luna/misc/log.hpp>

#include <std/minimal_vector.hpp>

#include <Luna/cpu/cpu.hpp>
#include <Luna/cpu/regs.hpp>
#include <Luna/cpu/idt.hpp>
#include <Luna/cpu/smp.hpp>
#include <Luna/cpu/tsc.hpp>

#include <Luna/cpu/threads.hpp>

#include <Luna/mm/pmm.hpp>
#include <Luna/mm/vmm.hpp>
#include <Luna/mm/hmm.hpp>

#include <Luna/drivers/gpu/lfb/lfb.hpp>
#include <Luna/drivers/gpu/lfb/vbe.hpp>
#include <Luna/drivers/usb/usb.hpp>
#include <Luna/drivers/iommu/iommu.hpp>
#include <Luna/drivers/acpi.hpp>
#include <Luna/drivers/ioapic.hpp>
#include <Luna/drivers/pci.hpp>
#include <Luna/drivers/timers/hpet.hpp>

#include <Luna/fs/vfs.hpp>

#include <Luna/vmm/vm.hpp>
#include <Luna/vmm/drivers/e9.hpp>
#include <Luna/vmm/drivers/uart.hpp>
#include <Luna/vmm/drivers/nvme.hpp>
#include <Luna/vmm/drivers/hpet.hpp>
#include <Luna/vmm/drivers/cmos.hpp>
#include <Luna/vmm/drivers/ps2.hpp>
#include <Luna/vmm/drivers/fast_a20.hpp>
#include <Luna/vmm/drivers/pit.hpp>
#include <Luna/vmm/drivers/io_delay.hpp>
#include <Luna/vmm/drivers/pci/pci.hpp>
#include <Luna/vmm/drivers/pci/pio_access.hpp>
#include <Luna/vmm/drivers/pci/ecam.hpp>
#include <Luna/vmm/drivers/pci/hotplug.hpp>

#include <Luna/vmm/drivers/q35/dram.hpp>
#include <Luna/vmm/drivers/q35/lpc.hpp>
#include <Luna/vmm/drivers/q35/acpi.hpp>
#include <Luna/vmm/drivers/q35/smi.hpp>

#include <Luna/vmm/drivers/irqs/lapic.hpp>
#include <Luna/vmm/drivers/irqs/8259.hpp>
#include <Luna/vmm/drivers/irqs/ioapic.hpp>

#include <Luna/vmm/drivers/gpu/bga.hpp>
#include <Luna/vmm/drivers/gpu/vga.hpp>

#include <Luna/net/luna_debug.hpp>

#include <Luna/gui/gui.hpp>
#include <Luna/gui/windows/log_window.hpp>

#include <std/mutex.hpp>
#include <std/event_queue.hpp>

static std::minimal_vector<CpuData, 1> per_cpu_data{};
static IrqTicketLock cpu_data_lock{};

static CpuData& allocate_cpu_data() {
    std::lock_guard guard{cpu_data_lock};

    return per_cpu_data.emplace_back();
}

void kernel_main_ap(stivale2_smp_info* info);
void create_vm();

void kernel_main(const stivale2_struct* info) {
    log::select_logger(log::LoggerType::Early);
    print("Booting Luna, Copyright Thomas Woertman 2020 - 2022\nBootloader: {:s} {:s}\n", info->bootloader_brand, info->bootloader_version);

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
    msr::write(msr::ia32_pat, msr::pat::default_pat); // Can't move this into cpu::early_init() because the current pagemap has a different PAT
    kernel_vmm.set();
    print("vmm: Set kernel page tables\n");

    hmm::init();
    print("hmm: Initialized SLAB allocator\n");

    lfb::init(boot_info);
    log::select_logger(log::LoggerType::Late);

    cpu_data.lapic.init();

    acpi::init_tables(boot_info);
    ioapic::init();

    hpet::init();
    tsc::init_per_cpu(); // TODO: Unify timer init?

    acpi::init_system();

    smp::start_cpus(boot_info, kernel_main_ap);    

    pci::init();

    vm::init();

    spawn([] {
        pci::handoff_bios();
        iommu::init();
        pci::init_drivers();

        gui::init();

        usb::init_devices();


        gui::get_desktop().start_gui(); // Only start GUI until after USB devices are mounted
        //vbe::init();

        create_vm();
        while(1)
            ;
    });

    threading::start_on_cpu();
    __builtin_unreachable();
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

    tsc::init_per_cpu();

    vm::init();

    threading::start_on_cpu();
    __builtin_unreachable();
}

constexpr size_t bsp_stack_size = 0x4000;
uint8_t bsp_stack[bsp_stack_size];

stivale2_header_tag_framebuffer fb_tab {
    .tag = {.identifier = STIVALE2_HEADER_TAG_FRAMEBUFFER_ID, .next = 0},
    .framebuffer_width = 0,
    .framebuffer_height = 0,
    .framebuffer_bpp = 32
};

stivale2_header_tag_smp smp_tag = {
    .tag = {.identifier = STIVALE2_HEADER_TAG_SMP_ID, .next = (uint64_t)&fb_tab},
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

void create_vm() {
    this_thread()->pin_to_this_cpu(); // TODO: Is it possible to migrate vcpus between cpus?

    constexpr uintptr_t himem_start = 0x10'0000;
    constexpr size_t himem_size = 128 * 1024 * 1024; // 16MiB

    vm::Vm vm{1, this_thread()};
    {
        auto* file = vfs::get_vfs().open("A:/luna/bios.bin");
        ASSERT(file);

        auto bios_size = file->get_size();
        ASSERT((bios_size % 0x1000) == 0);

        
        auto isa_bios_size = min(bios_size, 128 * 1024);
        auto isa_bios_start = himem_start - isa_bios_size;
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

        // Setup himem
        for(size_t i = 0; i < himem_size; i += 0x1000) {
            auto block = pmm::alloc_block();
            ASSERT(block);

            auto* va = (uint8_t*)(block + phys_mem_map);
            memset(va, 0, pmm::block_size);

            vm.mm->map(block, himem_start + i, paging::mapPagePresent | paging::mapPageWrite | paging::mapPageExecute);
        }

        file->close();
    }

    auto* cmos_dev = new vm::cmos::Driver{&vm};

    {
        auto size = himem_start + himem_size - (16 * 1024 * 1024); // TODO: Investigate this, seems to be what Seabios does
        cmos_dev->write(vm::cmos::cmos_extmem2_low, (size >> 16) & 0xFF);
        cmos_dev->write(vm::cmos::cmos_extmem2_high, (size >> 24) & 0xFF);


        /*
            Boot Priority Numbers
                1: Floppy
                2: Harddisk
                3: CD-Rom
                4: BEV???

            1st priority is Bootflag2 bits 0:3
            2nd priority is Bootflag2 bits 4:7
            3rd priority is Bootflag1 bits 4:7

            We do HDD -> CD -> Floppy
        */
        cmos_dev->write(vm::cmos::cmos_bootflag1, (1 << 4) | 0); // Bit0 = Disable Floppy MBR Sig Check
        cmos_dev->write(vm::cmos::cmos_bootflag2, (3 << 4) | (2 << 0));

        cmos_dev->write(vm::cmos::cmos_ap_count, 0); // Currently we only support the BSP, no APs


        cmos_dev->write(vm::cmos::rtc_day, 28); // TODO: Don't hardcode this
        cmos_dev->write(vm::cmos::rtc_month, 2);
        cmos_dev->write(vm::cmos::rtc_year, 21);
        cmos_dev->write(vm::cmos::rtc_century, 20);
    }
    
    auto* pci_host_bridge = new vm::pci::HostBridge{};


    auto* a20_dev = new vm::fast_a20::Driver{&vm};
    (void)a20_dev;

    auto* io_delay_dev = new vm::io_delay::Driver{&vm};
    (void)io_delay_dev;

    auto* log_window = new gui::LogWindow{{60, 40}, "VM Log"};
    gui::get_desktop().add_window(log_window);

    auto* uart_dev = new vm::uart::Driver{&vm, 0x3F8, log_window};
    (void)uart_dev;

    auto* e9_dev = new vm::e9::Driver{&vm, log_window};
    (void)e9_dev;

    auto* ps2_dev = new vm::ps2::Driver{&vm};
    (void)ps2_dev;

    auto* hpet_dev = new vm::hpet::Driver{&vm};
    (void)hpet_dev;

    auto* pit_dev = new vm::pit::Driver{&vm};
    (void)pit_dev;

    auto* file = vfs::get_vfs().open("A:/disk.bin");
    auto* nvme_dev = new vm::nvme::Driver{&vm, pci_host_bridge, 16, 0, file};
    (void)nvme_dev;

    auto* vgabios = vfs::get_vfs().open("A:/luna/vgabios.bin");
    ASSERT(vgabios);

    {
        uint8_t signature[2] = {};
        vgabios->read(0, 2, signature);

        ASSERT(signature[0] == 0x55 && signature[1] == 0xAA);
    }

    auto* bga_dev = new vm::gpu::bga::Driver{&vm, pci_host_bridge, vgabios, 2};
    (void)bga_dev;

    auto* vga_dev = new vm::gpu::vga::Driver{&vm};
    (void)vga_dev;

    auto* pci_hotplug = new vm::pci::hotplug::Driver{&vm};
    (void)pci_hotplug;

    auto* pci_pio_access = new vm::pci::pio_access::Driver{&vm, vm::pci::pio_access::default_base, 0, pci_host_bridge};
    (void)pci_pio_access;

    auto* pci_mmio_access = new vm::pci::ecam::Driver{&vm, pci_host_bridge, 0};
    (void)pci_mmio_access;

    auto* dram_dev = new vm::q35::dram::Driver{&vm, pci_host_bridge, pci_mmio_access};

    vm.cpus[0].set(vm::VmCap::SMMEntryCallback, [](vm::VCPU*, void* dram) { ((vm::q35::dram::Driver*)dram)->smm_enter(); }, dram_dev);
    vm.cpus[0].set(vm::VmCap::SMMLeaveCallback, [](vm::VCPU*, void* dram) { ((vm::q35::dram::Driver*)dram)->smm_leave(); }, dram_dev);

    auto* smi_dev = new vm::q35::smi::Driver{&vm};

    auto* acpi_dev = new vm::q35::acpi::Driver{&vm, smi_dev};

    auto* lpc_dev = new vm::q35::lpc::Driver{&vm, pci_host_bridge, acpi_dev};
    (void)lpc_dev;

    auto* pic_dev = new vm::irqs::pic::Driver{&vm};
    vm.irq_listeners.push_back(pic_dev);

    auto* ioapic_dev = new vm::irqs::ioapic::Driver{&vm, 1, 0xFEC0'0000};
    (void)ioapic_dev;
    
    vm.cpus[0].run();

    while(1)
        ;
}