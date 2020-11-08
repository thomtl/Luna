#include <Luna/drivers/intel/vt_d.hpp>
#include <Luna/misc/format.hpp>

#include <Luna/mm/pmm.hpp>
#include <Luna/mm/vmm.hpp>

#include <Luna/cpu/idt.hpp>
#include <Luna/drivers/pci.hpp>

vt_d::RemappingEngine::RemappingEngine(vt_d::Drhd* drhd): drhd{drhd} {
    auto va = drhd->mmio_base + phys_mem_map;
    vmm::kernel_vmm::get_instance().map(drhd->mmio_base, va, paging::mapPagePresent | paging::mapPageWrite);

    regs = (RemappingEngineRegs*)va;
    fault_recording_regs = (FaultRecordingRegister*)(va + (16 * ((regs->capabilities >> 24) & 0x3FF)));
    iotlb_regs = (IOTLB*)(va + (16 * ((regs->extended_capabilities >> 8) & 0x3FF)));

    const auto d = *drhd;
    ASSERT(d.flags.pci_include_all);
    print("    - DMA Remapping Engine\n");
    print("      PCI Segment: {:d}\n", d.segment);
    print("      MMIO Base: {:#x}\n", d.mmio_base);
    print("      Version {}.{}\n", (uint64_t)((regs->version >> 4) & 0xF), (uint64_t)(regs->version & 0xF));

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

    if(regs->extended_capabilities & (1 << 4))
        x2apic_mode = true;

    print("      Setting up IRQ ... ");

    uint32_t destination_id = get_cpu().lapic_id;
    auto vector = idt::allocate_vector();

    pci::msi::Address msi_addr{.raw = 0};
    pci::msi::Data msi_data{.raw = 0};
    RemappingEngineRegs::FaultEventUpperAddress msi_upper_addr{.raw = 0};

    msi_addr.base_address = 0xFEE;
    msi_addr.destination_id = destination_id & 0xFF;

    msi_data.delivery_mode = 0;
    msi_data.vector = vector;

    if(x2apic_mode)
        msi_upper_addr.upper_dest_id = (destination_id >> 24) & 0xFFFFFF;

    regs->fault_event_data = msi_data.raw;
    regs->fault_event_address = msi_addr.raw;
    regs->fault_event_upper_address = msi_upper_addr.raw;

    idt::set_handler(vector, idt::handler{.f = []([[maybe_unused]] idt::regs*, void* userptr) {
        auto& self = *(vt_d::RemappingEngine*)userptr;

        for(size_t i = 0; i < self.n_fault_recording_regs; i++) {
            uint64_t* reg = (uint64_t*)&self.fault_recording_regs[i];

            FaultRecordingRegister fault;
            uint64_t r0 = reg[0], r1 = reg[1];
            memcpy(&fault, &r0, 8);
            memcpy((uint8_t*)&fault + 8, &r1, 8);    

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

                auto reason = fault.reason;
                switch (reason) {
                    case 5: print("      Reason: A Write or AtomicOp request encountered lack of write permission.\n"); break;
                    default: print("      Reason: Unknown ({:#x})\n", reason);
                }
            }
        }

        asm("cli\nhlt");
    }, .is_irq = true, .should_iret = false, .userptr = this});

    regs->fault_event_control &= ~(1ull << 31); // Unmask IRQ

    print("Done\n      Installing Root Table ... ");

    auto root_block = pmm::alloc_block();
    ASSERT(root_block);
    auto root_table_va = root_block + phys_mem_map;
    vmm::kernel_vmm::get_instance().map(root_block, root_table_va, paging::mapPagePresent | paging::mapPageWrite);
    root_table = (RootTable*)root_table_va;
    memset((void*)root_table, 0, pmm::block_size);

    RemappingEngineRegs::RootTableAddress root_addr{};
    root_addr.address = root_block >> 12;
    root_addr.translation_type = 0; // Legacy Mode

    wbflush();
    regs->root_table_address = root_addr.raw;

    regs->global_command |= (1 << 30); // Update root ptr

    while(!(regs->global_status & (1 << 30)))
        asm("pause");

    this->invalidate_global_context();
    this->invalidate_iotlb();

    print("Done\n      Enabling Translation ... ");

    regs->global_command = (1 << 31);
    while(!(regs->global_status & (1 << 31)))
        asm("pause");

    print("Done\n");
}

void vt_d::RemappingEngine::wbflush() {
    if(!(regs->capabilities & (1 << 4)))
        return; // Flushing unsupported

    regs->global_command = (1 << 27);

    while(regs->global_status & (1 << 27))
        asm("pause");
}

void vt_d::RemappingEngine::invalidate_global_context() {
    regs->context_command = (1ull << 63) | (1ull << 61);

    while(regs->context_command & (1ull << 63))
        asm("pause");
}

void vt_d::RemappingEngine::invalidate_iotlb() {
    wbflush();

    IOTLBCmd cmd{};

    cmd.drain_reads = 1;
    cmd.drain_writes = 1;
    cmd.req_granularity = 1; // Global invalidation
    cmd.invalidate = 1;

    iotlb_regs->cmd = cmd.raw;

    while(iotlb_regs->cmd & (1ull << 63))
        asm("pause");
}

sl_paging::context& vt_d::RemappingEngine::get_device_translation(vt_d::SourceID device) {
    auto* root_entry = &root_table->entries[device.bus];
    if(!root_entry->present) {
        auto pa = pmm::alloc_block();
        if(!pa)
            PANIC("Couldn't allocate IOMMU table");
        auto va = pa + phys_mem_map;

        memset((void*)va, 0, pmm::block_size);
        
        root_entry->context_table = (pa >> 12);
        root_entry->present = 1;
    }

    auto& context_table = *(ContextTable*)((root_entry->context_table << 12) + phys_mem_map);
    uint8_t context_table_index = device.raw & 0xFF; // Slot and Function
    auto* context_table_entry = &context_table[context_table_index];

    if(!context_table_entry->present) {
        context_table_entry->translation_type = 0; // Legacy Translation
        context_table_entry->address_width = (secondary_page_levels - 2);

        auto domain_id = domain_ids.get_free_bit();
        if(domain_id == ~0u)
            PANIC("No Domain IDs left");

        context_table_entry->domain_id = domain_id;

        auto* context = new sl_paging::context{secondary_page_levels};
        page_map[device.raw] = context;

        context_table_entry->sl_translation_ptr = (context->get_root_pa() >> 12);
        context_table_entry->present = 1;
    }

    return *page_map[device.raw];
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
            print(" IRQ Remap\n");
        if(dmar->flags.x2APIC_opt_out)
            print(" x2APIC opt out\n");
        if(dmar->flags.dma_control_opt_in)
            print(" DMA control\n");
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
        }

        offset += *(type + 1);
    }
}

sl_paging::context& vt_d::IOMMU::get_translation(const pci::Device& device) {
    SourceID id{};
    id.bus = device.bus;
    id.slot = device.slot;
    id.func = device.func;

    for(auto& engine : engines)
        if(engine.segment == device.seg)
            return engine.get_device_translation(id);

    PANIC("Couldn't find engine for segment");
}