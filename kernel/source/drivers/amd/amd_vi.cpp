#include <Luna/drivers/amd/amd_vi.hpp>

#include <Luna/cpu/idt.hpp>

#include <Luna/misc/format.hpp>

#include <Luna/mm/pmm.hpp>
#include <Luna/mm/vmm.hpp>

amd_vi::IOMMUEngine::IOMMUEngine(amd_vi::Type10IVHD* ivhd): segment{ivhd->pci_segment}, domain_ids{n_domains}, ivhd{ivhd} {
    auto regs_pa = ivhd->iommu_base;
    auto regs_va = regs_pa + phys_mem_map;
    vmm::kernel_vmm::get_instance().map(regs_pa, regs_va, paging::mapPagePresent | paging::mapPageWrite);
    regs = (volatile IOMMUEngineRegs*)regs_va;

    {
        DeviceID id{.raw = ivhd->device_id};
        pci_dev = pci::device_by_location(ivhd->pci_segment, id.bus, id.slot, id.func);
        ASSERT(pci_dev);

        auto header = pci_dev->read<uint32_t>(ivhd->capability_offset + 0x0);
        auto revision = (header >> 19) & 0x1F;
        print("     - Remapping Engine v{}\n", revision);

        auto range = pci_dev->read<uint32_t>(ivhd->capability_offset + 0xC);
        start = DeviceID{.raw = (uint16_t)((((range >> 8) & 0xFF) << 8) | ((range >> 16) & 0xFF))};
        end = DeviceID{.raw = (uint16_t)((((range >> 8) & 0xFF) << 8) | ((range >> 24) & 0xFF))};

        print("       PCI Range Start: {}.{}.{}.{}\n", segment, (uint16_t)start.bus, (uint16_t)start.slot, (uint16_t)start.func);
        print("       PCI Range End: {}.{}.{}.{}\n", segment, (uint16_t)end.bus, (uint16_t)end.slot, (uint16_t)end.func);

        max_device_id = end.raw;
    }

    domain_ids.set(0); // Reserve Domain 0 for caching modes

    page_levels = 4 + ((regs->extended_features >> 10) & 0b11);
    if(page_levels == 6)
        page_levels = 5; // TODO: Support 6 level paging
    print("       Page Levels: {}\n", (uint16_t)page_levels);

    // According to the spec we should program the SMI Filter first, but linux doesn't seem to do it
    // TODO: Program SMI Filter, How do you figure out the BMC DeviceID??
    
    disable();
    init_flags();

    {
        // This is what linux does, however that info should be available in the Capabilities too as done above
        //max_device_id = get_highest_device_id();

        auto device_table_size = (max_device_id + 1) * sizeof(DeviceTableEntry);
        auto n_pages = div_ceil(device_table_size, pmm::block_size);

        auto device_table_pa = pmm::alloc_n_blocks(n_pages);
        ASSERT(device_table_pa);

        device_table = (volatile DeviceTableEntry*)(device_table_pa + phys_mem_map);
        memset((void*)device_table, 0, n_pages * pmm::block_size);

        print("       Installing Device Table ... ");

        uint64_t table_base = 0;
        table_base |= device_table_pa;
        table_base |= (n_pages - 1) & 0x1FF;

        regs->device_table_base = table_base;
        print("Done\n");

        for(size_t i = 0; i <= max_device_id; i++) {
            device_table[i].valid = 1;
            device_table[i].translation_info_valid = 1;
        }
    }

    {
        cmd_sem_pa = pmm::alloc_block();
        ASSERT(cmd_sem_pa);

        cmd_sem = (volatile uint64_t*)(cmd_sem_pa + phys_mem_map);
        memset((void*)cmd_sem, 0, pmm::block_size);

        auto cmd_ring_pa = pmm::alloc_block();
        ASSERT(cmd_ring_pa);

        cmd_ring.ring = (volatile uint8_t*)(cmd_ring_pa + phys_mem_map);
        memset((void*)cmd_ring.ring, 0, pmm::block_size);

        print("       Installing Command Ring ... ");
        uint64_t cmd_ring_base = 0;
        cmd_ring_base |= cmd_ring_pa;
        cmd_ring_base |= (0b1000ull << 56); // 4K => 256 entries

        regs->command_buffer_base = cmd_ring_base;

        regs->cmd_evt_ptrs.cmd_buf_head = 0;
        regs->cmd_evt_ptrs.cmd_buf_tail = 0;

        cmd_ring.head = 0;
        cmd_ring.tail = 0;
        cmd_ring.length = 256;
        cmd_ring.need_sync = false;

        regs->control |= (uint64_t)EngineControl::CommandBufferEnable;
        print("Done\n");
    }

    {
        auto evt_ring_pa = pmm::alloc_block();
        ASSERT(evt_ring_pa);

        evt_ring.ring = (volatile uint8_t*)(evt_ring_pa + phys_mem_map);
        memset((void*)evt_ring.ring, 0, pmm::block_size);

        print("       Installing Event Ring ... ");
        uint64_t evt_ring_base = 0;
        evt_ring_base |= evt_ring_pa;
        evt_ring_base |= (0b1000ull << 56); // 4K => 256 entries

        regs->event_log_base = evt_ring_base;

        regs->cmd_evt_ptrs.event_log_head = 0;
        regs->cmd_evt_ptrs.event_log_tail = 0;

        evt_ring.head = 0;
        evt_ring.tail = 0;
        evt_ring.length = 256;
        cmd_ring.need_sync = false;

        auto vector = idt::allocate_vector();

        idt::set_handler(vector, {.f = [](idt::regs*, void*){
            PANIC("TODO: AMD-Vi IRQ");
        }, .is_irq = true, .should_iret = false, .userptr = nullptr});

        pci_dev->enable_irq(vector);

        regs->control |= (uint64_t)EngineControl::EventIRQEnable;
        regs->control |= (uint64_t)EngineControl::EventLogEnable;

        regs->iommu_status = (1 << 0) | (1 << 1);
        print("Done\n");
    }

    {
        uintptr_t exclusion_start = 0;
        uintptr_t exclusion_length = 0;

        if(exclusion_start) {
            regs->exclusion_base = exclusion_start | (1 << 0); // Exclusion Enable
            regs->exclusion_limit = exclusion_length;

            print("       Setup Exclusion Registers\n");
        }
    }

    regs->control |= (uint64_t)EngineControl::IOMMUEnable;
    flush_all_caches();
    print("       Enabled IOMMU Engine\n");
}

void amd_vi::IOMMUEngine::disable() {
    regs->control &= ~(uint64_t)EngineControl::CommandBufferEnable; // Disable Commands

    regs->control &= ~(uint64_t)EngineControl::EventIRQEnable; // Disable Event logging
    regs->control &= ~(uint64_t)EngineControl::EventLogEnable;

    regs->control &= ~(uint64_t)EngineControl::GuestVAPICGALogEnable; // Disable GA Log
    regs->control &= ~(uint64_t)EngineControl::GuestVAPICIRQEnable;

    regs->control &= ~(uint64_t)EngineControl::IOMMUEnable; // Disable IOMMU itself
}

void amd_vi::IOMMUEngine::init_flags() {
    auto ivhd_feature = [this](uint64_t bit, EngineControl control) {
        if(ivhd->flags & (1 << bit))
            regs->control |= (uint64_t)control;
        else
            regs->control &= ~(uint64_t)control;
    };

    ivhd_feature(0, EngineControl::HyperTransportTunnelEnable);
    ivhd_feature(1, EngineControl::PassPostedWrite);
    ivhd_feature(2, EngineControl::ResponsePassPostedWrite);
    ivhd_feature(3, EngineControl::Isochronous);

    regs->control |= (uint64_t)EngineControl::Coherent;

    {
        auto control = regs->control;

        control &= ~(uint64_t)EngineControl::ControlInvalidateTimeoutMask;
        control |= (4 << (uint64_t)EngineControl::ControlInvalidateTimeout) & (uint64_t)EngineControl::ControlInvalidateTimeoutMask; // 4 => 1 Second

        regs->control = control;
    }
}

uint16_t amd_vi::IOMMUEngine::get_highest_device_id() {
    uint16_t highest_id = 0;
    auto update = [&highest_id](uint16_t id) { if(id > highest_id) highest_id = id; };

    size_t header_size = 0;
    switch (ivhd->type) {
        case 0x10: header_size = 24; break;
        case 0x11: [[fallthrough]];
        case 0x40: header_size = 40; break;
        default: PANIC("Unknown IVHD Header type");
    }

    auto* entries = (uint8_t*)ivhd + header_size;
    for(size_t i = 0; i < (ivhd->length - header_size);) {
        const auto& entry = *(IVHDEntry*)(entries + i);
        auto type = (IVHDEntryTypes)entry.type;

        switch (type) {
        case IVHDEntryTypes::DeviceAll:
            update(0xFFFF);
            break;
        case IVHDEntryTypes::DeviceSelect: [[fallthrough]];
        case IVHDEntryTypes::DeviceRangeEnd: [[fallthrough]];
        case IVHDEntryTypes::DeviceAlias: [[fallthrough]];
        case IVHDEntryTypes::DeviceExtSelect:
            update(entry.device_id);
            break;
        default:
            break;
        }

        size_t entry_size = 0;
        if(entry.type < 0x80)
            entry_size = (4 << (entry.type >> 6));
        else {
            print("amdvi: Unknown IVHD entry type {:#x}\n", (uint16_t)type);
            PANIC("Unknown Entry Type");
        }

        i += entry_size;
    }

    return highest_id;
}

void amd_vi::IOMMUEngine::cmd_invalidate_devtab_entry(const amd_vi::DeviceID& device) {
    CmdInvalidateDevTabEntry cmd{};
    cmd.op = CmdInvalidateDevTabEntry::opcode;

    cmd.device_id = device.raw;

    ASSERT(queue_command(cmd));
    completion_wait();
}

void amd_vi::IOMMUEngine::cmd_invalidate_all() {
    CmdInvalidateIOMMUAll cmd{};
    cmd.op = CmdInvalidateIOMMUAll::opcode;

    ASSERT(queue_command(cmd));
    completion_wait();
}

void amd_vi::IOMMUEngine::flush_all_caches() {
    // INVALIDATE_IOMMU_ALL Supported
    if(regs->extended_features & (1 << 6)) {
        cmd_invalidate_all();
    } else 
        PANIC("TODO: Implement manual all invalidation");
}

bool amd_vi::IOMMUEngine::queue_command(const uint8_t* cmd, size_t size) {
    auto next_tail = (cmd_ring.tail + size) % (cmd_ring.length * 16);

    uintptr_t left = 0;
    do {
        left = (cmd_ring.head - next_tail) % (cmd_ring.length * 16);

        cmd_ring.head = regs->cmd_evt_ptrs.cmd_buf_head;
    } while(left <= size);

    memcpy((void*)(cmd_ring.ring + cmd_ring.tail), cmd, size);
    cmd_ring.tail = next_tail;

    regs->cmd_evt_ptrs.cmd_buf_tail = cmd_ring.tail; // Ring ring

    cmd_ring.need_sync = true;

    return true;
}

void amd_vi::IOMMUEngine::completion_wait() {
    if(!cmd_ring.need_sync)
        return;

    CmdCompletionWait cmd{};
    cmd.op = CmdCompletionWait::opcode;
    cmd.store = 1;
    cmd.store_address = (cmd_sem_pa >> 3);
    cmd.store_data = 1;

    ASSERT(queue_command(cmd));

    while(*cmd_sem == 0)
        asm("pause");

    cmd_ring.need_sync = false;
}

io_paging::context& amd_vi::IOMMUEngine::get_translation(const amd_vi::DeviceID& device) {
    if(device.raw > max_device_id)
        PANIC("Trying to access device table over max device id");

    auto& entry = device_table[device.raw];
    if(entry.domain_id == 0) {
        auto domain_id = domain_ids.get_free_bit();
        if(domain_id == ~0u)
            PANIC("No Domain IDs left");
        domain_ids.set(domain_id);

        domain_id_map[device.raw] = domain_id;

        auto* context = new io_paging::context{page_levels};
        page_map[device.raw] = context;

        entry.valid = 1;
        entry.translation_info_valid = 1;
        entry.paging_mode = (page_levels & 0b111);
        entry.dirty_control = 0;
        entry.domain_id = domain_id;
        entry.page_table_root_ptr = (context->get_root_pa() >> 12);

        entry.io_read_permission = 1;
        entry.io_write_permission = 1;

        cmd_invalidate_devtab_entry(device);
    }

    return *page_map[device.raw];
}

void amd_vi::IOMMUEngine::invalidate_iotlb_addr(const DeviceID& device, uintptr_t iova) {
    auto domain_id = domain_id_map[device.raw];

    CmdInvalidateIOMMUPages cmd{};
    cmd.op = CmdInvalidateIOMMUPages::opcode;
    cmd.s = 0; // 4k Invalidation
    cmd.pde = 1;
    cmd.domain_id = domain_id;
    cmd.address = (iova >> 12);

    ASSERT(queue_command(cmd));
    completion_wait();
}

amd_vi::IOMMU::IOMMU() {
    ivrs = acpi::get_table<Ivrs>();
    if(!ivrs)
        return;

    auto revision = ivrs->header.revision;
    ASSERT(revision == 0x1 || revision == 0x2);

    print("amdvi: IVRS v{} Found\n", (uint16_t)revision);

    Ivrs::IvInfo iv{.raw = ivrs->iv_info};
    ASSERT(!iv.preboot_dma);

    size_t size = ivrs->header.length - sizeof(Ivrs);
    for(size_t i = 0; i < size;) {
        uint8_t* header = &ivrs->ivdb[i];
        if(header[0] == 0x10) {
            auto* ivhd = (Type10IVHD*)header;

            engines.emplace_back(ivhd);
        } else {
            print("       Unknown IVRS Entry Type {:#x}\n", (uint16_t)header[0]);
        }

        i += header[1];
    }
}

amd_vi::IOMMUEngine& amd_vi::IOMMU::engine_for_device(uint16_t seg, const DeviceID& id) {
    for(auto& engine : engines)
        if(engine.segment == seg && id >= engine.start && id <= engine.end)
            return engine;

    PANIC("No engine for segment");
}
        
void amd_vi::IOMMU::map(const pci::Device& device, uintptr_t pa, uintptr_t iova, uint64_t flags) {
    auto id = DeviceID::from_device(device);

    auto& engine = engine_for_device(device.seg, id);

    engine.get_translation(id).map(pa, iova, flags);
    engine.invalidate_iotlb_addr(id, iova);
}

uintptr_t amd_vi::IOMMU::unmap(const pci::Device& device, uintptr_t iova) {
    auto id = DeviceID::from_device(device);

    auto& engine = engine_for_device(device.seg, id);

    auto ret = engine.get_translation(id).unmap(iova);
    engine.invalidate_iotlb_addr(id, iova);

    return ret;
}

void amd_vi::IOMMU::invalidate_iotlb_entry(const pci::Device& device, uintptr_t iova) {
    auto id = DeviceID::from_device(device);

    auto& engine = engine_for_device(device.seg, id);

    engine.invalidate_iotlb_addr(id, iova);
}

bool amd_vi::has_iommu() {
    return acpi::get_table<Ivrs>() ? true : false;
}