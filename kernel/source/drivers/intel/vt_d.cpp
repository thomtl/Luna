#include <Luna/drivers/intel/vt_d.hpp>
#include <Luna/misc/format.hpp>

#include <Luna/mm/pmm.hpp>
#include <Luna/mm/vmm.hpp>

#include <Luna/cpu/idt.hpp>
#include <Luna/drivers/pci.hpp>
#include <Luna/drivers/ioapic.hpp>

static vt_d::SourceID parse_path(uint16_t segment, const vt_d::DeviceScope& dev) {
    size_t n = (dev.length - 6) / 2;

    vt_d::SourceID cur{};
    cur.bus = dev.start_bus;
    cur.slot = dev.path[0].device;
    cur.func = dev.path[0].function;

    for(size_t i = 1; i < n; i++) {
        vt_d::SourceID next{};
        next.bus = pci::read<uint8_t>(segment, cur.bus, cur.slot, cur.func, 19); // Secondary bus number
        next.slot = dev.path[i].device;
        next.func = dev.path[i].function;

        cur = next;
    }

    return cur;
};

vt_d::InvalidationQueue::InvalidationQueue(volatile vt_d::RemappingEngineRegs* regs): regs{regs} {
    ASSERT(regs->extended_capabilities & (1 << 1));

    queue_pa = pmm::alloc_block();
    ASSERT(queue_pa);
    auto queue_va = queue_pa + phys_mem_map;
    vmm::kernel_vmm::get_instance().map(queue_pa, queue_va, paging::mapPagePresent | paging::mapPageWrite); // Hardware access to the IQ is always snooped

    queue = (uint8_t*)queue_va;

    memset((void*)queue, 0, pmm::block_size);
    
    head = 0;
    tail = 0;
}

vt_d::InvalidationQueue::~InvalidationQueue() {
    if(queue_pa)
        pmm::free_block(queue_pa);
}

void vt_d::InvalidationQueue::queue_command(const uint8_t* cmd) {
    auto next_tail = (tail + 1) % queue_length;

    memcpy((void*)(queue + (tail * queue_entry_size)), cmd, queue_entry_size);
    tail = next_tail;
}

void vt_d::InvalidationQueue::submit_sync(const uint8_t* cmd) {
    InvalidationWaitDescriptor wait{};
    wait.type = InvalidationWaitDescriptor::cmd;
    wait.irq = 1;

    queue_command(cmd);
    queue_command((uint8_t*)&wait);

    regs->invalidation_queue_tail = (tail * queue_entry_size);
    
    while((regs->invalidation_completion_status & 1) == 0) {
        // Invalidation Queue Error
        if(regs->fault_status & (1 << 4)) {
            PANIC("Invalidation Queue Error");
            regs->fault_status = (1 << 4);
        }

        if(regs->fault_status & (1 << 5)) {
            PANIC("Invalidation Completion Error\n");
            regs->fault_status = (1 << 5);
        }

        // Invalidation Time-out
        if(regs->fault_status & (1 << 6)) {
            PANIC("Invalidation Queue Timeout");
            regs->fault_status = (1 << 6);
        }
    }

    regs->invalidation_completion_status = 1;
}

vt_d::RemappingEngine::RemappingEngine(vt_d::Drhd* drhd): drhd{drhd}, global_command{0} {
    auto va = drhd->mmio_base + phys_mem_map;
    vmm::kernel_vmm::get_instance().map(drhd->mmio_base, va, paging::mapPagePresent | paging::mapPageWrite, paging::cacheDisable);

    regs = (RemappingEngineRegs*)va;
    fault_recording_regs = (FaultRecordingRegister*)(va + (16 * ((regs->capabilities >> 24) & 0x3FF)));
    iotlb_regs = (IOTLB*)(va + (16 * ((regs->extended_capabilities >> 8) & 0x3FF)));

    const auto& d = *drhd;
    print("    - DMA Remapping Engine\n");
    print("      PCI Segment: {:d}\n", d.segment);
    print("      Version {}.{}\n", (uint64_t)((regs->version >> 4) & 0xF), (uint64_t)(regs->version & 0xF));

    if(d.flags.pci_include_all)
        print("      Includes all devices on Segment\n");

    all_devices_on_segment = d.flags.pci_include_all;

    print("      Device Scopes: \n");
    auto* scope = d.device_scope;
    auto scope_end = scope + (d.length - sizeof(d));
    while(scope < scope_end) {
        const auto& dev = *(DeviceScope*)scope;

        if(dev.type == 1) {
            ASSERT(!d.flags.pci_include_all);

            auto sid = parse_path(d.segment, dev);
            source_id_ranges.push_back({sid, sid});

            auto bus = sid.bus; auto slot = sid.slot; auto func = sid.func;
            print("       - PCI Endpoint Device: {}.{}.{}.{}\n", d.segment, bus, slot, func);
        } else {
            print("       - Unknown type: {:#x}\n", (uint16_t)dev.type);
        }

        scope += dev.length;
    }

    segment = d.segment;

    print("      SL Page levels supported: ");
    if(((regs->capabilities >> 8) & 0x1F) & (1 << 1)) { secondary_page_levels = 3; print("3;"); }
    if(((regs->capabilities >> 8) & 0x1F) & (1 << 2)) { secondary_page_levels = 4; print("4;"); }
    if(((regs->capabilities >> 8) & 0x1F) & (1 << 3)) { secondary_page_levels = 5; print("5;"); }
    print("\n");
    ASSERT(secondary_page_levels != 0);

    n_fault_recording_regs = ((regs->capabilities >> 40) & 0xFF) + 1;
    print("      Fault Recording Regs: {:d}\n", n_fault_recording_regs);

    n_domain_ids = 1 << (2 * (regs->capabilities & 0x7) + 4);
    print("      Domain IDs: {:d}\n", n_domain_ids);

    domain_ids = std::bitmap{n_domain_ids};
    if(regs->capabilities & (1 << 7)) // If cap.CM is set domain ID 0 is reserved
        domain_ids.set(0);

    wbflush_needed = (regs->capabilities & (1 << 4)) ? true : false;
    write_draining = (regs->capabilities & (1ull << 54)) ? true : false;
    read_draining = (regs->capabilities & (1ull << 55)) ? true : false;
    x2apic_mode = (regs->extended_capabilities & (1 << 4)) ? true : false;
    page_selective_invalidation = (regs->capabilities & (1ull << 39)) ? true : false;
    if(regs->extended_capabilities & (1 << 0)) // Page-walk Coherency
        page_cache_mode = paging::cacheWriteback;
    else
        page_cache_mode = paging::cacheDisable;

    print("      Setting up IRQ ... ");
    wbflush();

    uint32_t destination_id = get_cpu().lapic_id;
    auto vector = idt::allocate_vector();

    pci::msi::Address msi_addr{.raw = 0};
    pci::msi::Data msi_data{.raw = 0};

    msi_addr.base_address = 0xFEE;
    msi_addr.destination_id = destination_id & 0xFF;

    msi_data.delivery_mode = 0;
    msi_data.vector = vector;

    regs->fault_event_data = msi_data.raw;
    regs->fault_event_address = msi_addr.raw;

    if(x2apic_mode) {
        RemappingEngineRegs::FaultEventUpperAddress msi_upper_addr{.raw = 0};
        msi_upper_addr.upper_dest_id = (destination_id >> 24) & 0xFFFFFF;
        
        regs->fault_event_upper_address = msi_upper_addr.raw;
    }

    idt::set_handler(vector, idt::handler{.f = []([[maybe_unused]] idt::regs* regs, void* userptr) {
        auto& self = *(vt_d::RemappingEngine*)userptr;
        self.handle_irq(); 
    }, .is_irq = true, .should_iret = false, .userptr = this});

    regs->fault_event_control &= ~(1u << 31); // Unmask IRQ
    (void)(regs->fault_event_control); // Force flush

    print("Done\n");

    print("      Installing Root Table ... ");

    auto root_block = pmm::alloc_block();
    ASSERT(root_block);

    auto root_table_va = root_block + phys_mem_map;
    vmm::kernel_vmm::get_instance().map(root_block, root_table_va, paging::mapPagePresent | paging::mapPageWrite, page_cache_mode);

    root_table = (RootTable*)root_table_va;
    memset((void*)root_table, 0, pmm::block_size);

    RemappingEngineRegs::RootTableAddress root_addr{};
    root_addr.address = (root_block >> 12);
    root_addr.translation_type = 0; // Legacy Mode

    wbflush();
    regs->root_table_address = root_addr.raw;

    regs->global_command |= GlobalCommand::SetRootTablePointer; // Update root ptr

    while(!(regs->global_status & GlobalCommand::SetRootTablePointer))
        asm("pause");

    print("Done\n");

    if(regs->extended_capabilities & (1 << 1)) {
        print("      Setting up Invalidation Queue ... ");
        iq.init(regs, page_cache_mode);

        {
            RemappingEngineRegs::InalidationQueueAddress addr{};
            addr.size = (InvalidationQueue::queue_page_size - 1); // 2^N pages
            addr.descriptor_width = 0; // 128-bit descriptors
            addr.address = (iq->get_queue_pa() >> 12);

            regs->invalidation_queue_address = addr.raw;
        }

        regs->invalidation_queue_tail = 0;

        global_command |= GlobalCommand::QueuedInvalidationEnable; // Enable Queued Invalidations, NO NORMAL IOTLB BEYOND THIS POINT
        regs->global_command = global_command;
        while(!(regs->global_status & GlobalCommand::QueuedInvalidationEnable))
            asm("pause");

        print("Done\n");
    }

    print("      Enabling Translation ... ");

    this->invalidate_global_context();
    this->invalidate_global_iotlb();

    global_command |= GlobalCommand::TranslationEnable;
    regs->global_command = global_command;
    while(!(regs->global_status & GlobalCommand::TranslationEnable))
        asm("pause");

    print("Done\n");
}

void vt_d::RemappingEngine::wbflush() {
    if(!wbflush_needed)
        return;

    regs->global_command = global_command | GlobalCommand::FlushWriteBuffer;

    while(regs->global_status & GlobalCommand::FlushWriteBuffer)
        asm("pause");
}

void vt_d::RemappingEngine::invalidate_global_context() {
    wbflush();

    if(iq) {
        ContextInvalidationDescriptor cmd{};
        cmd.type = ContextInvalidationDescriptor::cmd;
        cmd.granularity = 0b01; // Global Invalidation

        iq->submit_sync((uint8_t*)&cmd);
    } else {
        regs->context_command = (1ull << 63) | (1ull << 61);

        while(regs->context_command & (1ull << 63))
            asm("pause");
    }
}

void vt_d::RemappingEngine::invalidate_global_iotlb() {
    wbflush();

    if(iq) {
        IOTLBInvalidationDescriptor cmd{};
        cmd.type = IOTLBInvalidationDescriptor::cmd;
        
        cmd.drain_reads = read_draining ? 1 : 0;
        cmd.drain_writes = write_draining ? 1 : 0;
        cmd.granularity = 0b01; // Global Invalidation

        iq->submit_sync((uint8_t*)&cmd);
    } else {
        IOTLBCmd cmd{};

        cmd.drain_reads = read_draining ? 1 : 0;
        cmd.drain_writes = write_draining ? 1 : 0;
        cmd.req_granularity = 0b01; // Global invalidation
        cmd.invalidate = 1;

        iotlb_regs->cmd = cmd.raw;

        while(iotlb_regs->cmd & (1ull << 63))
            asm("pause");
    }
}

void vt_d::RemappingEngine::invalidate_domain_iotlb(SourceID device) {
    wbflush();

    if(iq) {
        IOTLBInvalidationDescriptor cmd{};
        cmd.type = IOTLBInvalidationDescriptor::cmd;

        cmd.drain_reads = read_draining ? 1 : 0;
        cmd.drain_writes = write_draining ? 1 : 0;
        cmd.granularity = 0b10; // Domain
        cmd.domain_id = domain_id_map[device.raw];
        
        iq->submit_sync((uint8_t*)&cmd);
    } else {
        IOTLBCmd cmd{};
        cmd.drain_reads = read_draining ? 1 : 0;
        cmd.drain_writes = write_draining ? 1 : 0;
        cmd.req_granularity = 0b10; // Domain
        cmd.domain_id = domain_id_map[device.raw];
        cmd.invalidate = 1;

        iotlb_regs->cmd = cmd.raw;

        while(iotlb_regs->cmd & (1ull << 63))
            asm("pause");
    }
}

void vt_d::RemappingEngine::invalidate_iotlb_addr(SourceID device, uintptr_t iova) {
    if(!page_selective_invalidation)
        return invalidate_domain_iotlb(device);
    
    wbflush();
    if(iq) {
        IOTLBInvalidationDescriptor cmd{};
        cmd.type = IOTLBInvalidationDescriptor::cmd;

        cmd.drain_reads = read_draining ? 1 : 0;
        cmd.drain_writes = write_draining ? 1 : 0;
        cmd.granularity = 0b11; // Domain + Addr Local
        cmd.domain_id = domain_id_map[device.raw];
        cmd.addr = iova >> 12;
        
        iq->submit_sync((uint8_t*)&cmd);
    } else {
        IOTLBCmd cmd{};
        cmd.drain_reads = read_draining ? 1 : 0;
        cmd.drain_writes = write_draining ? 1 : 0;
        cmd.req_granularity = 0b11; // Domain local invalidation + Addr
        cmd.domain_id = domain_id_map[device.raw];
        cmd.invalidate = 1;

        IOTLBAddr addr{};
        addr.addr = iova >> 12;

        iotlb_regs->addr = addr.raw;
        iotlb_regs->cmd = cmd.raw;

        while(iotlb_regs->cmd & (1ull << 63))
            asm("pause");
    }
}

sl_paging::context& vt_d::RemappingEngine::get_device_translation(vt_d::SourceID device) {
    auto* root_entry = &root_table->entries[device.bus];
    if(!root_entry->present) {
        auto pa = pmm::alloc_block();
        if(!pa)
            PANIC("Couldn't allocate IOMMU context table");
        auto va = pa + phys_mem_map;
        vmm::kernel_vmm::get_instance().map(pa, va, paging::mapPagePresent | paging::mapPageWrite, page_cache_mode);

        memset((void*)va, 0, pmm::block_size);
        
        root_entry->context_table = (pa >> 12);
        root_entry->present = 1;
    }

    auto& context_table = *(ContextTable*)((root_entry->context_table << 12) + phys_mem_map);
    uint8_t context_table_index = device.raw & 0xFF; // Slot and Function
    auto* context_table_entry = &context_table[context_table_index];

    if(!context_table_entry->present) {
        auto domain_id = domain_ids.get_free_bit();
        if(domain_id == ~0u)
            PANIC("No Domain IDs left");
        domain_ids.set(domain_id);

        domain_id_map[device.raw] = domain_id;

        auto* context = new sl_paging::context{secondary_page_levels, page_cache_mode};
        page_map[device.raw] = context;

        context_table_entry->translation_type = 0; // Legacy Translation
        context_table_entry->address_width = (secondary_page_levels - 2);
        context_table_entry->domain_id = domain_id;
        context_table_entry->sl_translation_ptr = (context->get_root_pa() >> 12);
        context_table_entry->present = 1;
    }

    return *page_map[device.raw];
}

void vt_d::RemappingEngine::handle_irq() {
    auto fault_status = regs->fault_status;
    if((fault_status & (1 << 1)) == 0) // No actual faults
        return;

    size_t fault_index = (fault_status >> 8) & 0xFF;

    for(size_t i = fault_index; i < n_fault_recording_regs; i++) {
        uint64_t* reg = (uint64_t*)&fault_recording_regs[i];

        FaultRecordingRegister fault{};
        //uint64_t r0 = reg[0], r1 = reg[1];
        //memcpy(&fault, &r0, 8);
        //memcpy((uint8_t*)&fault + 8, &r1, 8);    
        fault.raw.a = reg[0];
        fault.raw.b = reg[1];

        if(fault.fault) {
            print("vt-d: Fault at index {}\n", i);

            uint8_t type = fault.type_bit_1 | (fault.type_bit_2 << 1);
            switch (type) {
                case 0: print("      Type: Write Request\n"); break;
                case 1: print("      Type: Read Request\n"); break;
                case 2: print("      Type: Page Request\n"); break;
                case 3: print("      Type: AtomicOp Request\n"); break;
                default: print("      Type: Unknown\n"); break;
            }

            const SourceID sid{.raw = (uint16_t)fault.source_id};
            print("      SID: {}:{}.{}\n", sid.bus, sid.slot, sid.func);

            print("      Privilege: {:s}\n", fault.supervisor ? "Supervisor" : "User");
            if(fault.execute)
                print("      Execute access\n");

            print("      IOVA: {:#x}\n", fault.fault_info << 12);

            auto reason = fault.reason;
            switch (reason) {
                case 1: print("      Reason: The Present field in the root-entry used to process a request is 0.\n"); break;
                case 5: print("      Reason: A Write or AtomicOp request encountered lack of write permission.\n"); break;
                case 0xA: print("      Reason: Non-zero reserved field in a root-entry with the Present field set\n"); break;
                default: print("      Reason: Unknown ({:#x})\n", reason);
            }
        }
    }

    asm("cli\nhlt");
}

vt_d::IOMMU::IOMMU() {
    dmar = acpi::get_table<Dmar>();
    if(!dmar)
        return;

    print("vt-d: DMAR Found\n");
    print("    - Host Address Width: {:d}\n", dmar->host_address_width + 1);

    if(dmar->flags.raw) {
        print("    - Flags:");
        if(dmar->flags.irq_remap)
            print(" IRQ Remap ");
        if(dmar->flags.x2APIC_opt_out)
            print(" x2APIC opt out ");
        if(dmar->flags.dma_control_opt_in)
            print(" DMA control ");
        print("\n");
    }

    for(size_t offset = sizeof(Dmar); offset < dmar->header.length;) {
        auto* type = (uint16_t*)((uintptr_t)dmar + offset);

        switch (*type) {
            case Drhd::id: {
                auto* drhd = (Drhd*)type;
                engines.emplace_back(drhd);
                break;
            }
            case Rmrr::id: {
                auto* rmrr = (Rmrr*)type;
                auto segment = rmrr->segment; auto base = rmrr->base; auto limit = rmrr->limit;
                print("    - RMRR: {:#x} -> {:#x}\n", base, limit);
                
                print("      Device Scopes: \n");
                auto* scope = rmrr->device_scope;
                auto scope_end = scope + (rmrr->length - sizeof(Rmrr));
                while(scope < scope_end) {
                    const auto& dev = *(DeviceScope*)scope;

                    if(dev.type == 1) {
                        auto sid = parse_path(segment, dev);

                        auto bus = sid.bus; auto slot = sid.slot; auto func = sid.func;
                        print("       - PCI Endpoint Device: {}.{}.{}.{} ... ", segment, bus, slot, func);

                        auto* dev = pci::device_by_location(segment, bus, slot, func);
                        ASSERT(dev);

                        for(size_t i = base; i < limit; i += pmm::block_size)
                            map(*dev, i, i, paging::mapPagePresent | paging::mapPageWrite);

                        print("Mapped\n");
                    } else {
                        print("       - Unknown type: {:#x}\n", (uint16_t)dev.type);
                    }

                    scope += dev.length;
                }
                break;
            }

            default:
                print("    - Unknown DMAR entry type {:#x}\n", *type);
                break;
        }

        offset += *(type + 1);
    }
}

vt_d::RemappingEngine& vt_d::IOMMU::get_engine(uint16_t seg, SourceID source) {
    for(auto& engine : engines) {
        if(engine.segment == seg) {
            if(engine.all_devices_on_segment)
                return engine; // If it includes all devices on segment we already found it
            
            for(const auto [begin, end] : engine.source_id_ranges)
                if(source >= begin && source <= end)
                    return engine;
        }
    }

    PANIC("Couldn't find engine for segment");
}

void vt_d::IOMMU::map(const pci::Device& device, uintptr_t pa, uintptr_t iova, uint64_t flags) {
    auto id = SourceID::from_device(device);

    auto& engine = get_engine(device.seg, id);

    std::lock_guard guard{engine.lock};

    engine.get_device_translation(id).map(pa, iova, flags);
    engine.invalidate_iotlb_addr(id, iova); // Technicaly not needed in all cases
    engine.wbflush();
}

uintptr_t vt_d::IOMMU::unmap(const pci::Device& device, uintptr_t iova) {
    auto id = SourceID::from_device(device);

    auto& engine = get_engine(device.seg, id);

    std::lock_guard guard{engine.lock};

    auto ret = engine.get_device_translation(id).unmap(iova);
    engine.invalidate_iotlb_addr(id, iova); // Technicaly not needed in all cases
    engine.wbflush();

    return ret;
}

void vt_d::IOMMU::invalidate_iotlb_entry(const pci::Device& device, uintptr_t iova) {
    auto id = SourceID::from_device(device);

    auto& engine = get_engine(device.seg, id);

    std::lock_guard guard{engine.lock};
    
    return engine.invalidate_iotlb_addr(id, iova);
}

bool vt_d::has_iommu() {
    return acpi::get_table<Dmar>() ? true : false;
}