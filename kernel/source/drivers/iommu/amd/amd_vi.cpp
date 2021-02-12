#include <Luna/drivers/iommu/amd/amd_vi.hpp>

#include <Luna/cpu/idt.hpp>

#include <Luna/misc/log.hpp>

#include <Luna/mm/pmm.hpp>
#include <Luna/mm/vmm.hpp>

#include <std/mutex.hpp>

amd_vi::IOMMUEngine::IOMMUEngine(const amd_vi::IVHDInfo& ivhd): segment{ivhd.segment}, domain_ids{n_domains}, ivhd{ivhd} {
    auto regs_pa = ivhd.base;
    auto regs_va = regs_pa + phys_mem_map;
    vmm::kernel_vmm::get_instance().map(regs_pa, regs_va, paging::mapPagePresent | paging::mapPageWrite);
    regs = (volatile IOMMUEngineRegs*)regs_va;

    {
        DeviceID id{.raw = ivhd.device_id};
        pci_dev = pci::device_by_location(ivhd.segment, id.bus, id.slot, id.func);
        ASSERT(pci_dev);

        pci_dev->set_privileges(pci::privileges::Pio | pci::privileges::Mmio | pci::privileges::Dma);

        auto header = pci_dev->read<uint32_t>(ivhd.capability_offset + 0x0);
        auto revision = (header >> 19) & 0x1F;
        print("     - Remapping Engine v{}\n", revision);

        if(header & (1 << 26))
            non_present_cache = true;
    }

    domain_ids.set(0); // Reserve Domain 0 for caching modes

    page_levels = 4 + ((regs->extended_features >> 10) & 0b11);
    if(page_levels == 6)
        page_levels = 5; // TODO: Support 6 level paging
    print("       Page Levels: {}\n", (uint16_t)page_levels);

    erratum_746_workaround();
    ats_write_check_workaround();

    // According to the spec we should program the SMI Filter first, but linux doesn't seem to do it
    // TODO: Program SMI Filter, How do you figure out the BMC DeviceID??
    
    disable();
    init_flags();

    {
        max_device_id = get_highest_device_id(); // get_highest_device_id will also initialize the device_id ranges
        print("       Max DeviceID: {:#x}\n", max_device_id);

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
    }

    {
        cmd_sem_pa = pmm::alloc_block();
        ASSERT(cmd_sem_pa);

        cmd_sem = (volatile uint64_t*)(cmd_sem_pa + phys_mem_map);
        memset((void*)cmd_sem, 0, pmm::block_size);

        cmd_sem_val = 0;

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

        idt::set_handler(vector, {.f = [](uint8_t, idt::regs*, void* userptr){
            auto& self = *(IOMMUEngine*)userptr;
            auto status = self.regs->iommu_status;
            while(status & (1 << 1)) {
                self.regs->iommu_status = (1 << 1);

                self.evt_ring.head = self.regs->cmd_evt_ptrs.event_log_head;
                self.evt_ring.tail = self.regs->cmd_evt_ptrs.event_log_tail;

                while(self.evt_ring.head != self.evt_ring.tail) {
                    auto* evt = (uint32_t*)(self.evt_ring.ring + self.evt_ring.head);
                    auto type = evt[1] >> 28;
                    if(type == 0b10) {
                        const auto pf = *(EvtIOPageFault*)evt;
                        DeviceID device{.raw = (uint16_t)pf.device_id};

                        print("amdvi: I/O Page Fault\n");
                        auto bus = device.bus, slot = device.slot, func = device.func;
                        print("       Device: {}.{}.{}.{}\n", self.segment, bus, slot, func);
                        print("       Address {:#x}\n", pf.address);
                        print("       {} Privileges\n", pf.user ? "User" : "Supervisor");

                        if(pf.interrupt_request) {
                            print("       Interrupt Request\n");
                        } else {
                            print("       Memory Request\n");

                            auto* io = self.page_map[device.raw];
                            auto a = device.raw;
                            if(!io)
                                print("       Device ({:#x}) has no IO Paging structures\n", a);
                            else {
                                auto page = io->get_page(pf.address);

                                if(page.present)
                                    print("       Page: {:#x}, {}{}\n", page.frame << 12, page.r ? "R" : "", page.w ? "W" : "");
                            }
                        }

                        if(pf.translation) {
                            print("       Translation Request\n");
                        } else {
                            print("       Transaction Request\n");
                            if(!pf.present) {
                                print("       Not present\n");
                            } else {
                                print("       {} Operation\n", pf.write ? "Write" : "Read");
                                print("{}", pf.reserved_bit_set ? "       Reserved bit was set\n" : "");
                            }
                        }
                    } else if(type == 0b101) {
                        print("amdvi: Illegal Command Error\n");
                        const auto err = *(EvtIllegalCmd*)evt;
                        auto* cmd = (uint32_t*)(err.address + phys_mem_map);

                        auto op = cmd[1] >> 28;
                        print("       Op address: {:#x}\n", err.address);
                        print("       cw0: {:#x}, cw1: {:#x}, cw2: {:#x}, cw3: {:#x}\n", cmd[0], cmd[1], cmd[2], cmd[3]);
                        print("       Opcode: {:#b}\n", op);
                    } else {
                        print("amdvi: Unknown event ring type: {:#b}\n", (uint16_t)type);
                    }

                    memset(evt, 0, 16);

                    self.evt_ring.head = (self.evt_ring.head + 16) % 0x1000;
                }

                self.regs->cmd_evt_ptrs.event_log_head = self.evt_ring.head;

                // Some errata, the iommu might re-set everything, and we need to loop through again
                status = self.regs->iommu_status;
            }
        }, .is_irq = true, .should_iret = false, .userptr = this});

        pci_dev->enable_irq(0, vector);

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

    {
        // flush_all_caches() will flush the devtable later
        for(size_t i = 0; i <= max_device_id; i++)
            device_table[i].data[0] |= 1; // Set valid, disallow all accesses, when valid = 1 but translation_info_valid = 0

        auto update_device_info_from_acpi = [&](uint16_t devid, uint8_t flags) {
            DeviceTableEntry entry{};
            entry.load(&device_table[devid]);

            #define BIT_TEST(n, field) \
                if(flags & (1 << n)) \
                    entry.field = 1;

            BIT_TEST(0, init_pass);
            BIT_TEST(1, einit_pass);
            BIT_TEST(2, nmi_pass);
            BIT_TEST(4, sysmgt1);
            BIT_TEST(5, sysmgt2);
            BIT_TEST(6, lint0_pass);
            BIT_TEST(7, lint1_pass);

            // Apply Erratum 63
            uint8_t sysmgt = entry.sysmgt1 | (entry.sysmgt2 << 1);
            if(sysmgt == 1)
                entry.io_write_permission = 1;

            entry.store(&device_table[devid]);
        };

        auto* entries = ivhd.devices;
        uint16_t devid_start = 0, devid_to = 0;
        uint8_t flags = 0;
        uint32_t ext_flags = 0;
        bool alias = false;
        for(size_t i = 0; i < ivhd.devices_length;) {
            const auto& entry = *(IVHDEntry*)(entries + i);
            auto type = (IVHDEntryTypes)entry.type;

            switch (type) {
            case IVHDEntryTypes::DeviceAll:
                for(size_t i = 0; i <= max_device_id; i++)
                    update_device_info_from_acpi(i, entry.flags);
                break;
            case IVHDEntryTypes::DeviceSelect:
                update_device_info_from_acpi(entry.device_id, entry.flags);
                break;
            case IVHDEntryTypes::DeviceAlias:
                update_device_info_from_acpi(entry.device_id, entry.flags);
                update_device_info_from_acpi((entry.ext >> 8) & 0xFFFF, entry.flags);

                alias_map[entry.device_id] = (entry.ext >> 8) & 0xFFFF;
                break;

            case IVHDEntryTypes::DeviceSelectRangeStart:
                devid_start = entry.device_id;
                flags = entry.flags;
                ext_flags = entry.ext;
                alias = false;
                break;
            case IVHDEntryTypes::DeviceRangeEnd:
                (void)ext_flags;
                for(size_t i = devid_start; i <= entry.device_id; i++) {
                    if(alias) {
                        alias_map[i] = devid_to;
                        update_device_info_from_acpi(devid_to, flags);
                    } else
                        update_device_info_from_acpi(i, flags);
                }
                break;
            case IVHDEntryTypes::DeviceAliasRange:
                devid_start = entry.device_id;
                devid_to = entry.ext >> 8;
                flags = entry.flags;
                ext_flags = 0;
                alias = true;
                break;
            case IVHDEntryTypes::DeviceSpecial:
                update_device_info_from_acpi((entry.ext >> 8) & 0xFFFF, entry.flags);
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
        if(ivhd.flags & (1 << bit))
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

uint32_t amd_vi::IOMMUEngine::read_l2(uint8_t addr) {
    pci_dev->write<uint32_t>(0xF0, addr);
    return pci_dev->read<uint32_t>(0xF4);
}

void amd_vi::IOMMUEngine::write_l2(uint8_t addr, uint32_t v) {
    pci_dev->write<uint32_t>(0xF0, addr | (1 << 8));
    pci_dev->write<uint32_t>(0xF4, v);
}

void amd_vi::IOMMUEngine::erratum_746_workaround() {
    // https://elixir.bootlin.com/linux/v5.9.10/source/drivers/iommu/amd/init.c#L1403

    auto& cpu = get_cpu().cpu;
    if(cpu.family != 0x15 || cpu.model < 0x10 || cpu.model > 0x1F)
        return;
    
    auto v = read_l2(0x90);
    if(v & (1 << 2)) // Erratum already applied by firmware
        return;

    write_l2(0xF4, v | (1 << 2));
    print("       Applied Erratum 746 Workaround\n");
}

void amd_vi::IOMMUEngine::ats_write_check_workaround() {
    // https://elixir.bootlin.com/linux/v5.9.10/source/drivers/iommu/amd/init.c#L1434

    auto& cpu = get_cpu().cpu;
    if(cpu.family != 0x15 || cpu.model < 0x30 || cpu.model > 0x3F)
        return;

    auto v = read_l2(0x47);
    if(v & 1) // Workaround already applied by firmware
        return;
    
    write_l2(0x47, v | 1);
    print("       Applied ATS Write Check Workaround\n");
}

uint16_t amd_vi::IOMMUEngine::get_highest_device_id() {
    uint16_t highest_id = 0;
    auto update = [&highest_id](uint16_t id) { if(id > highest_id) highest_id = id; };

    auto* entries = ivhd.devices;
    uint16_t devid_start = 0;
    for(size_t i = 0; i < ivhd.devices_length;) {
        const auto& entry = *(IVHDEntry*)(entries + i);
        auto type = (IVHDEntryTypes)entry.type;

        switch (type) {
        case IVHDEntryTypes::DeviceAll:
            device_id_ranges.push_back({0, 0xFFFF});
            update(0xFFFF);
            break;
        case IVHDEntryTypes::DeviceSelect: [[fallthrough]];
        case IVHDEntryTypes::DeviceExtSelect: [[fallthrough]];
        case IVHDEntryTypes::DeviceAlias:
            device_id_ranges.push_back({entry.device_id, entry.device_id});
            update(entry.device_id);
            break;
        case IVHDEntryTypes::DeviceSelectRangeStart:
            devid_start = entry.device_id;
            break;
        case IVHDEntryTypes::DeviceRangeEnd:
            device_id_ranges.push_back({devid_start, entry.device_id});
            update(entry.device_id);
            break;
        case IVHDEntryTypes::DeviceSpecial: {
            auto devid = (entry.ext >> 8) & 0xFFFF;
            device_id_ranges.push_back({devid, devid});
            break;
        }
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

    auto data = ++cmd_sem_val;

    CmdCompletionWait cmd{};
    cmd.op = CmdCompletionWait::opcode;
    cmd.store = 1;
    cmd.store_address = (cmd_sem_pa >> 3);
    cmd.store_data = data;

    ASSERT(queue_command(cmd));

    while(*cmd_sem != data)
        asm("pause");

    cmd_ring.need_sync = false;
}

io_paging::context& amd_vi::IOMMUEngine::get_translation(const amd_vi::DeviceID& device) {
    if(device.raw > max_device_id)
        PANIC("Trying to access device table over max device id");

    volatile auto* entry_ptr = &device_table[device.raw];
    DeviceTableEntry entry{};
    entry.load(entry_ptr);
    if(!entry.translation_info_valid) {
        auto domain_id = domain_ids.get_free_bit();
        if(domain_id == ~0u)
            PANIC("No Domain IDs left");
        domain_ids.set(domain_id);

        domain_id_map[device.raw] = domain_id;

        auto* context = new io_paging::context{page_levels};
        page_map[device.raw] = context;

        entry.translation_info_valid = 1;
        entry.paging_mode = (page_levels & 0b111);
        entry.dirty_control = 0;
        entry.domain_id = domain_id;
        entry.page_table_root_ptr = (context->get_root_pa() >> 12);

        entry.io_read_permission = 1;
        entry.io_write_permission = 1;

        entry.valid = 1;

        entry.store(entry_ptr);

        if(alias_map.contains(device.raw)) {
            auto alias = alias_map[device.raw];

            domain_id_map[alias] = domain_id;
            page_map[alias] = context;

            entry.store(&device_table[alias]);
        }
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

void amd_vi::IOMMUEngine::map(const DeviceID& device, uintptr_t pa, uintptr_t iova, uint64_t flags) {
    get_translation(device).map(pa, iova, flags);

    invalidate_iotlb_addr(device, iova);
}

uintptr_t amd_vi::IOMMUEngine::unmap(const DeviceID& device, uintptr_t iova) {
    auto ret = get_translation(device).unmap(iova);

    return ret;
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
    uint8_t highest_ivhd_type = 0xFF;
    auto devid = ((Type10IVHD*)&ivrs->ivdb[0])->device_id;
    for(size_t i = 0; (i < size) && (ivrs->ivdb[i] <= 0x40);) {
        auto* ivhd = (Type10IVHD*)&ivrs->ivdb[i];
        if(ivhd->device_id == devid)
            highest_ivhd_type = ivhd->type;
        
        i += ivhd->length;
    }

    for(size_t i = 0; i < size;) {
        uint8_t* header = &ivrs->ivdb[i];
        if(header[0] == Type10IVHD::sig) {
            if(highest_ivhd_type == Type10IVHD::sig) {
                auto* ivhd = (Type10IVHD*)header;

                IVHDInfo info{};
                info.base = ivhd->iommu_base;
                info.capability_offset = ivhd->capability_offset;
                info.device_id = ivhd->device_id;
                info.flags = ivhd->flags;
                info.segment = ivhd->pci_segment;

                info.devices_length = ivhd->length - 24; // Type 10 header is 24 bytes long
                info.devices = ivhd->ivhd_devices;

                engines.emplace_back(info);
            }
        } else if(header[0] == Type11IVHD::sig) {
            if(highest_ivhd_type == Type11IVHD::sig) {
                auto* ivhd = (Type11IVHD*)header;

                IVHDInfo info{};
                info.base = ivhd->iommu_base;
                info.capability_offset = ivhd->capability_offset;
                info.device_id = ivhd->device_id;
                info.flags = ivhd->flags;
                info.segment = ivhd->pci_segment;

                info.devices_length = ivhd->length - 40; // Type 11 header is 40 bytes long
                info.devices = ivhd->ivhd_devices;

                engines.emplace_back(info);
            }
        } else {
            print("       Unknown IVRS Entry Type {:#x}\n", (uint16_t)header[0]);
        }

        i += ((uint16_t*)header)[1];
    }
}

amd_vi::IOMMUEngine& amd_vi::IOMMU::engine_for_device(uint16_t seg, const DeviceID& id) {
    for(auto& engine : engines) {
        if(engine.segment == seg) {
            for(const auto [begin, end] : engine.device_id_ranges)
                if(id >= begin && id <= end)
                    return engine;
        }
    }

    PANIC("Couldn't find engine for segment");
}
        
void amd_vi::IOMMU::map(const pci::Device& device, uintptr_t pa, uintptr_t iova, uint64_t flags) {
    auto id = DeviceID::from_device(device);

    auto& engine = engine_for_device(device.seg, id);

    std::lock_guard guard{engine.lock};

    engine.map(id, pa, iova, flags);
}

uintptr_t amd_vi::IOMMU::unmap(const pci::Device& device, uintptr_t iova) {
    auto id = DeviceID::from_device(device);

    auto& engine = engine_for_device(device.seg, id);

    std::lock_guard guard{engine.lock};

    return engine.unmap(id, iova);
}

void amd_vi::IOMMU::invalidate_iotlb_entry(const pci::Device& device, uintptr_t iova) {
    auto id = DeviceID::from_device(device);

    auto& engine = engine_for_device(device.seg, id);

    std::lock_guard guard{engine.lock};

    engine.invalidate_iotlb_addr(id, iova);
}

bool amd_vi::has_iommu() {
    return acpi::get_table<Ivrs>() ? true : false;
}