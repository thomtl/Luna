#include <Luna/drivers/iommu/intel/vt_d.hpp>
#include <Luna/misc/log.hpp>

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

vt_d::RemappingEngine::RemappingEngine(vt_d::Drhd* drhd): drhd{drhd} {
    auto va = drhd->mmio_base + phys_mem_map;
    vmm::kernel_vmm::get_instance().map(drhd->mmio_base, va, paging::mapPagePresent | paging::mapPageWrite);

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

    wbflush_needed = (regs->capabilities >> 4) & 1;
    plmr = (regs->capabilities >> 5) & 1;
    phmr = (regs->capabilities >> 6) & 1;
    caching_mode = (regs->capabilities >> 7) & 1;
    zero_length_read = (regs->capabilities >> 22) & 1;
    page_selective_invalidation = (regs->capabilities >> 39) & 1;
    write_draining = (regs->capabilities >> 54) & 1;
    read_draining = (regs->capabilities >> 55) & 1;

    coherent = regs->extended_capabilities & 1;
    qi = (regs->extended_capabilities >> 1) & 1;
    eim = (regs->extended_capabilities >> 4) & 1;
    page_snoop = (regs->extended_capabilities >> 7) & 1;

    if(caching_mode)
        domain_ids.set(0);

    ASSERT(regs->global_status == 0);

    clear_faults();
    if(qi) {
        if(regs->global_status & (uint32_t)GlobalCommand::QueuedInvalidationEnable) {
            // QI was enabled from BIOS
            print("      QI was enabled in preboot, disabling ... ");

            // Give HW a chance to complete outstanding requests
            while(regs->invalidation_queue_head != regs->invalidation_queue_tail)
                asm("pause");

            regs->global_command = regs->global_status & ~(uint32_t)GlobalCommand::QueuedInvalidationEnable;

            while(regs->global_status & (uint32_t)GlobalCommand::QueuedInvalidationEnable)
                asm("pause");

            print("Done\n");
        }

        print("      Setting up Invalidation Queue ... ");
        iq.init(regs);

        {
            RemappingEngineRegs::InalidationQueueAddress addr{};
            addr.size = (InvalidationQueue::queue_page_size - 1); // 2^N pages
            addr.descriptor_width = 0; // 128-bit descriptors
            addr.address = (iq->get_queue_pa() >> 12);

            regs->invalidation_queue_address = addr.raw;
        }

        regs->invalidation_queue_tail = 0;

        regs->global_command = regs->global_status | GlobalCommand::QueuedInvalidationEnable; // Enable Queued Invalidations, NO NORMAL IOTLB BEYOND THIS POINT
        while(!(regs->global_status & GlobalCommand::QueuedInvalidationEnable))
            asm("pause");

        regs->invalidation_completion_status = 1;

        print("Done\n");
    }

    wbflush();

    print("      Setting up IRQ ... ");

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

    if(eim) {
        RemappingEngineRegs::FaultEventUpperAddress msi_upper_addr{.raw = 0};
        msi_upper_addr.upper_dest_id = (destination_id >> 24) & 0xFFFFFF;
        
        regs->fault_event_upper_address = msi_upper_addr.raw;
    }

    idt::set_handler(vector, idt::handler{.f = []([[maybe_unused]] uint8_t, [[maybe_unused]] idt::regs* regs, void* userptr) {
        auto& self = *(vt_d::RemappingEngine*)userptr;
        self.handle_irq(); 
    }, .is_irq = true, .should_iret = false, .userptr = this});

    regs->fault_event_control &= ~(1u << 31); // Unmask IRQ
    (void)(regs->fault_event_control); // Force flush

    regs->fault_status = 0x7F; // Clear all faults

    print("Done\n");

    print("      Installing Root Table ... ");

    auto root_block = pmm::alloc_block();
    ASSERT(root_block);

    auto root_table_va = root_block + phys_mem_map;
    vmm::kernel_vmm::get_instance().map(root_block, root_table_va, paging::mapPagePresent | paging::mapPageWrite, msr::pat::wb);

    root_table = (RootTable*)root_table_va;
    memset((void*)root_table, 0, pmm::block_size);

    flush_cache((void*)root_table, pmm::block_size);

    RemappingEngineRegs::RootTableAddress root_addr{};
    root_addr.address = (root_block >> 12);
    root_addr.translation_type = 0; // Legacy Mode

    wbflush();
    regs->root_table_address = root_addr.raw;

    regs->global_command = regs->global_status | GlobalCommand::SetRootTablePointer; // Update root ptr

    while(!(regs->global_status & GlobalCommand::SetRootTablePointer))
        asm("pause");

    invalidate_global_context();
    invalidate_global_iotlb();

    print("Done\n");
}

void vt_d::RemappingEngine::enable_translation() {
    regs->global_command = regs->global_status | GlobalCommand::TranslationEnable;
    while(!(regs->global_status & GlobalCommand::TranslationEnable))
        asm("pause");

    disable_protect_mem_regions();
}

void vt_d::RemappingEngine::flush_cache(void* va, size_t sz) {
    if(!coherent)
        cpu::cache_flush(va, sz);
}

void vt_d::RemappingEngine::wbflush() {
    if(!wbflush_needed)
        return;

    regs->global_command = regs->global_status | GlobalCommand::FlushWriteBuffer;

    while(regs->global_status & GlobalCommand::FlushWriteBuffer)
        asm("pause");
}

void vt_d::RemappingEngine::invalidate_global_context() {
    wbflush();
    
    if(iq) {
        ContextInvalidationDescriptor cmd{};
        cmd.type = ContextInvalidationDescriptor::cmd;
        cmd.granularity = ContextInvalidateGlobal;

        iq->submit_sync((uint8_t*)&cmd);
    } else {
        RemappingEngineRegs::ContextCommand cmd{};
        cmd.granularity = ContextInvalidateGlobal;
        cmd.invalidate = 1;

        regs->context_command = cmd.raw;

        while(regs->context_command & (1ull << 63))
            asm("pause");
    }
}

void vt_d::RemappingEngine::invalidate_device_context(uint16_t domain_id, SourceID device) {
    wbflush();

    if(iq) {
        ContextInvalidationDescriptor cmd{};
        cmd.type = ContextInvalidationDescriptor::cmd;
        cmd.granularity = ContextInvalidateDevice;
        cmd.domain_id = domain_id;
        cmd.source_id = device.raw;

        iq->submit_sync((uint8_t*)&cmd);
    } else {
        RemappingEngineRegs::ContextCommand cmd{};
        cmd.domain_id = domain_id;
        cmd.source_id = device.raw;
        cmd.granularity = ContextInvalidateDevice;
        cmd.invalidate = 1;

        regs->context_command = cmd.raw;

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
        cmd.granularity = IOTLBInvalidateGlobal;

        iq->submit_sync((uint8_t*)&cmd);
    } else {
        IOTLBCmd cmd{};
        cmd.drain_reads = read_draining ? 1 : 0;
        cmd.drain_writes = write_draining ? 1 : 0;
        cmd.req_granularity = IOTLBInvalidateGlobal;
        cmd.invalidate = 1;

        iotlb_regs->cmd = cmd.raw;

        while(iotlb_regs->cmd & (1ull << 63))
            asm("pause");
    }
}

void vt_d::RemappingEngine::invalidate_domain_iotlb(uint16_t domain_id) {
    wbflush();

    if(iq) {
        IOTLBInvalidationDescriptor cmd{};
        cmd.type = IOTLBInvalidationDescriptor::cmd;
        cmd.drain_reads = read_draining ? 1 : 0;
        cmd.drain_writes = write_draining ? 1 : 0;
        cmd.granularity = IOTLBInvalidateDomain;
        cmd.domain_id = domain_id;
        
        iq->submit_sync((uint8_t*)&cmd);
    } else {
        IOTLBCmd cmd{};
        cmd.drain_reads = read_draining ? 1 : 0;
        cmd.drain_writes = write_draining ? 1 : 0;
        cmd.req_granularity = IOTLBInvalidateDomain; // Domain
        cmd.domain_id = domain_id;
        cmd.invalidate = 1;

        iotlb_regs->cmd = cmd.raw;

        while(iotlb_regs->cmd & (1ull << 63))
            asm("pause");
    }
}

void vt_d::RemappingEngine::invalidate_iotlb_addr(uint16_t domain_id, uintptr_t iova) {
    if(!page_selective_invalidation)
        return invalidate_domain_iotlb(domain_id);

    wbflush();
    
    if(iq) {
        IOTLBInvalidationDescriptor cmd{};
        cmd.type = IOTLBInvalidationDescriptor::cmd;

        cmd.drain_reads = read_draining ? 1 : 0;
        cmd.drain_writes = write_draining ? 1 : 0;
        cmd.granularity = IOTLBInvalidatePage;
        cmd.domain_id = domain_id;
        cmd.addr = iova >> 12;
        
        iq->submit_sync((uint8_t*)&cmd);
    } else {
        IOTLBCmd cmd{};
        cmd.drain_reads = read_draining ? 1 : 0;
        cmd.drain_writes = write_draining ? 1 : 0;
        cmd.req_granularity = IOTLBInvalidatePage;
        cmd.domain_id = domain_id;
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
        vmm::kernel_vmm::get_instance().map(pa, va, paging::mapPagePresent | paging::mapPageWrite, msr::pat::wb);
        memset((void*)va, 0, pmm::block_size);
        flush_cache((void*)va, pmm::block_size);

        root_entry->context_table = (pa >> 12);
        root_entry->present = 1;

        flush_cache((void*)root_entry, sizeof(RootEntry));
    }

    auto& context_table = *(ContextTable*)((root_entry->context_table << 12) + phys_mem_map);
    uint8_t context_table_index = device.raw & 0xFF; // Slot and Function
    auto* context_table_entry = &context_table[context_table_index];

    if(!context_table_entry->present) {
        auto domain_id = domain_ids.get_free_bit();
        if(domain_id == ~0u)
            PANIC("No Domain IDs left");
        domain_ids.set(domain_id);

        auto* context = new sl_paging::context{secondary_page_levels, page_snoop, coherent};

        domain_id_map[device.raw] = domain_id;
        page_map[device.raw] = context;

        context_table_entry->domain_id = domain_id;
        context_table_entry->sl_translation_ptr = (context->get_root_pa() >> 12);
        context_table_entry->address_width = (secondary_page_levels - 2);
        context_table_entry->translation_type = 0; // Legacy Translation
        context_table_entry->fault_processing_disable = 0;
        context_table_entry->present = 1;
        flush_cache(context_table_entry, sizeof(ContextEntry));
        
        //if(caching_mode) {
            invalidate_device_context(0, device);
            invalidate_domain_iotlb(domain_id);
        //} else {
            //wbflush();
        //}
    }

    return *page_map[device.raw];
}

void vt_d::RemappingEngine::map(vt_d::SourceID device, uintptr_t pa, uintptr_t iova, uint64_t flags) {
    if(!zero_length_read)
        flags |= paging::mapPagePresent;
        
    get_device_translation(device).map(pa, iova, flags);

    if(caching_mode)
        invalidate_iotlb_addr(domain_id_map[device.raw], iova);
    else
        wbflush();
}

uintptr_t vt_d::RemappingEngine::unmap(vt_d::SourceID device, uintptr_t iova) {
    auto ret = get_device_translation(device).unmap(iova);

    invalidate_iotlb_addr(domain_id_map[device.raw], iova);
    return ret;
}

void vt_d::RemappingEngine::handle_primary_fault() {
    for(size_t i = 0; i < n_fault_recording_regs; i++) {
        uint64_t* reg = (uint64_t*)&fault_recording_regs[i];

        FaultRecordingRegister fault{};
        //uint64_t r0 = reg[0], r1 = reg[1];
        //memcpy(&fault, &r0, 8);
        //memcpy((uint8_t*)&fault + 8, &r1, 8);    
        fault.raw.a = reg[0];
        fault.raw.b = reg[1];

        if(fault.fault) {
            print("      Fault at index {}\n", i);

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
                case 1: print("      Reason: Non-present Root Entry\n"); break;
                case 5: print("      Reason: A Write or AtomicOp request encountered lack of write permission\n"); break;
                case 6: print("      Reason: A Read or AtomicOp request encountered lack of read permission.\n"); break;
                case 0xA: print("      Reason: Non-zero reserved field in root-entry with the Present field set\n"); break;
                default: print("      Reason: Unknown ({:#x})\n", reason);
            }

            volatile auto* root = (uint64_t*)(&root_table->entries[sid.bus]);
            print("      Root Entry: Lo: {:#x} Hi: {:#x}\n", root[0], root[1]);

            if(page_map.contains(fault.source_id)) {
                auto ent = page_map[fault.source_id]->get_entry(fault.fault_info << 12);
                print("      Page Entry: {}{}{}, PA: {:#x}\n", ent.r ? "R" : "", ent.w ? "W" : "", ent.x ? "X" : "", ent.frame << 12);
            }

            reg[1] = (1u << 31); // Clear fault
        }
    }

    asm("cli\nhlt");
}

void vt_d::RemappingEngine::handle_irq() {
    auto fault_status = regs->fault_status;
    print("vt-d: Handling IRQ, FSTS: {:#b}\n", fault_status);
    if(fault_status & (1 << 0)) {
        print("      Primary Fault Overflow\n");
        regs->fault_status = (1 << 0); 
    }

    if(fault_status & (1 << 1)) {
        handle_primary_fault();
    }    

    asm("cli\nhlt");
}

void vt_d::RemappingEngine::clear_faults() {
    for(size_t i = 0; i < n_fault_recording_regs; i++) {
        uint64_t* reg = (uint64_t*)&fault_recording_regs[i];

        if(reg[1] & (1u << 31)) {
            print("vt-d: Fault detected from BIOS\n");
            reg[1] = (1u << 31); // Clear fault
        }
    }    
}

void vt_d::RemappingEngine::disable_protect_mem_regions() {
    if(!plmr && !phmr)
        return;
    
    regs->protected_mem_enable &= ~(1u << 31);

    // Wait for protection region status to clear
    while(regs->protected_mem_enable & (1 << 0))
        asm("pause");
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

    print("    - Enabling DMA Translation ... ");
    for(auto& engine : engines)
        engine.enable_translation();
    print("Done\n");
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

    engine.map(id, pa, iova, flags);
}

uintptr_t vt_d::IOMMU::unmap(const pci::Device& device, uintptr_t iova) {
    auto id = SourceID::from_device(device);

    auto& engine = get_engine(device.seg, id);

    std::lock_guard guard{engine.lock};

    return engine.unmap(id, iova);
}

bool vt_d::has_iommu() {
    return acpi::get_table<Dmar>() ? true : false;
}