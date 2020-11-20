#include <Luna/drivers/storage/ahci.hpp>
#include <Luna/drivers/iommu.hpp>

#include <Luna/mm/vmm.hpp>

#include <Luna/misc/format.hpp>

ahci::Controller::Controller(pci::Device* device): device{device}, iommu_vmm{device} {
    auto bar = device->read_bar(5);
    ASSERT(bar.type == pci::Bar::Type::Mmio);

    device->set_privileges(pci::privileges::Dma | pci::privileges::Mmio);

    iommu::map(*device, bar.base, bar.base, paging::mapPagePresent | paging::mapPageWrite); // Allow aHCI to access its own MMIO region
    vmm::kernel_vmm::get_instance().map(bar.base, bar.base + phys_mem_map, paging::mapPagePresent | paging::mapPageWrite);
    regs = (volatile Hba*)(bar.base + phys_mem_map);

    print("ahci: Version {}.{}.{}\n", regs->ghcr.vs >> 16, (regs->ghcr.vs >> 8) & 0xFF, regs->ghcr.vs & 0xFF);

    if(!bios_handshake())
        return;

    regs->ghcr.ghc |= (1 << 31); // Enable aHCI

    a64 = (regs->ghcr.cap & (1 << 31)) != 0;

    // Add the usable iommu region, don't use 0x0 to avoid confusion with NULL, and limit to 32bits if those addresses are not supported
    if(a64)
        iommu_vmm.push_region({0x1000, 0xFFFF'FFFF'FFFF'FFFF - 0x1000});
    else
        iommu_vmm.push_region({0x1000, 0xFFFF'FFFF - 0x1000});

    n_allocated_ports = (regs->ghcr.cap & 0x1F) + 1;
    n_command_slots = ((regs->ghcr.cap >> 8) & 0x1F) + 1;

    ports.resize(n_allocated_ports);

    for(size_t i = 0; i < 32; i++) {
        if(!(regs->ghcr.pi & (1 << i)))
            continue; // Port is unimplemented

        auto& port = ports.emplace_back();
        port.port = i;
        port.controller = this;
        port.regs = (volatile Prs*)&regs->ports[i];

        uint8_t det = port.regs->ssts & 0xF;
        uint8_t ipm = (port.regs->ssts >> 8) & 0xFF;
        if(det != 3) {
            if(det == 0)
                continue; // No Device and no comms
            else if(det == 1)
                PANIC("Device but no comms, TODO implement COMRESET");
            else {
                print("ahci: Unknown PxSSTS.det value {} at port {}\n", (uint32_t)det, i);
                continue;
            }
        }
        
        if(ipm != 1)
            PANIC("TODO Get device out of sleep state");

        uint32_t sig = port.regs->sig;
        if(sig == 0)
            continue; // No device

        if(sig != 0x0101 && sig != 0xEB140101) {
            print("ahci: Unknown PxSIG ({:#x}) at port {:d}\n", sig, i);
            continue;
        }

        port.wait_idle();

        // TODO: This does not work for some reason, it makes qemu shit its pants
        // qemu-system-x86_64: /build/qemu-2AfuBA/qemu-4.2/exec.c:3572: address_space_unmap: Assertion `mr != NULL' failed.
        /*auto region = iommu_vmm.alloc(sizeof(Port::PhysRegion));
        port.region = (Port::PhysRegion*)region.host_base;*/

        auto region_pa = pmm::alloc_block();
        iommu::map(*device, region_pa, region_pa, paging::mapPagePresent | paging::mapPageWrite);

        port.region = (Port::PhysRegion*)(region_pa + phys_mem_map);

        auto cmd_header_addr = region_pa + offsetof(Port::PhysRegion, command_headers);
        port.regs->clb = cmd_header_addr & 0xFFFFFFFF;
        if(a64)
            port.regs->clbu = (cmd_header_addr >> 32) & 0xFFFFFFFF;

        auto fis_addr = region_pa + offsetof(Port::PhysRegion, receive_fis);
        port.regs->fb = fis_addr & 0xFFFFFFFF;
        if(a64)
            port.regs->fbu = (fis_addr >> 32) & 0xFFFFFFFF;

        port.regs->cmd |= (1 << 0) | (1 << 4);
        port.regs->is = 0xFFFFFFFF;

        for(size_t i = 0; i < 100000; i++)
            asm("pause");

        if(!(port.regs->cmd & (1 << 15)) || !(port.regs->cmd & (1 << 14))) {
            print("ahci: Failed to start CMD engine for port {}\n", i);
            continue;
        }

        ata::DriverDevice device{};
        device.atapi = (sig == 0xEB140101);
        device.userptr = (void*)&port; // Pointer will not change because we resized earlier
        device.ata_cmd = [](void* userptr, const ata::ATACommand& cmd, std::span<uint8_t>& xfer) {
            auto* port = (Controller::Port*)userptr;

            port->send_ata_cmd(cmd, xfer.data(), xfer.size());
        };

        if(device.atapi) { // Only enable the atapi cmd function if the device is actually atapi
            device.atapi_cmd = [](void* userptr, const ata::ATAPICommand& cmd, std::span<uint8_t>& xfer) {
                auto* port = (Controller::Port*)userptr;

                port->send_atapi_cmd(cmd, xfer.data(), xfer.size());
            };
        }

        ata::register_device(device);
    }

}

bool ahci::Controller::bios_handshake() {
    if(regs->ghcr.vs >= make_version(1, 2, 0)) {
        if((regs->ghcr.cap2 & (1 << 0)) == 0)
            return true;

        regs->ghcr.bohc |= (1 << 1); // Request Ownership

        for(size_t i = 0; i < 100000; i++)
            asm("pause");

        if(regs->ghcr.bohc & (1 << 4)) // Bios is busy wait a bit more
            for(size_t i = 0; i < 800000; i++)
                asm("pause");

        if(!(regs->ghcr.bohc & (1 << 1) && ((regs->ghcr.bohc & (1 << 0)) || (regs->ghcr.bohc & (1 << 4))))) {
            print("ahci: Failed to do BIOS handshake\n");
            return false;
        }

        regs->ghcr.bohc &= ~(1 << 3);
        return true;
    }

    return true;
}

void ahci::Controller::Port::wait_idle() {
    regs->cmd &= ~1; // Clear start

    while(regs->cmd & (1 << 15)) // Wait until command lists are no longer running
        asm("pause");

    regs->cmd &= ~(1 << 4); // Clear FIS Receive enable

    while(regs->cmd & (1 << 14)) // Wait until FIS are no longer received
        asm("pause");
}

void ahci::Controller::Port::wait_ready() {
    while((regs->tfd & (1 << 7)) || (regs->tfd & (1 << 3)))
        asm("pause");
}

uint8_t ahci::Controller::Port::get_free_cmd_slot() {
    for(size_t i = 0; i < controller->n_command_slots; i++)
        if((regs->ci & (1 << i)) == 0)
            return i;

    return ~0;
}

ahci::Controller::Port::Command ahci::Controller::Port::allocate_command(size_t n_prdts) {
    auto i = get_free_cmd_slot();
    if(i == ~0u)
        return {(uint8_t)~0, nullptr, {}};

    ASSERT(i < 32);

    auto& header = region->command_headers[i];

    size_t size = sizeof(CmdTable) + (n_prdts * sizeof(Prdt));
    auto region = controller->iommu_vmm.alloc(size, iovmm::Iovmm::Bidirectional);

    header.ctba = region.guest_base & 0xFFFF'FFFF;
    if(controller->a64)
        header.ctbau = (region.guest_base >> 32) & 0xFFFF'FFFF;

    return {i, (ahci::CmdTable*)region.host_base, region};
}

void ahci::Controller::Port::free_command(const Command& cmd) {
    controller->iommu_vmm.free(cmd.allocation);
}

void ahci::Controller::Port::send_ata_cmd(const ata::ATACommand& cmd, uint8_t* data, size_t transfer_len) {
    size_t n_prdts = div_ceil(transfer_len, 0x3FFFFF);
    auto slot = allocate_command(n_prdts);
    ASSERT(slot.index != ~0);

    region->command_headers[slot.index].flags.prdtl = n_prdts;
    region->command_headers[slot.index].flags.write = cmd.write;
    region->command_headers[slot.index].flags.cfl = 5; // h2d register is 5 dwords

    auto& fis = *(H2DRegisterFIS*)slot.table->fis;
    fis.type = FISTypeH2DRegister;
    fis.flags.c = 1;

    fis.command = cmd.command;
    fis.control = 0x8;
    fis.dev_head = 0xA0 | (1 << 6) | ((cmd.lba28) ? ((cmd.lba >> 24) & 0xF) : (0)); // Obsolete stuff + LBA mode + Top nybble in case of lba28
    fis.sector_count_low = cmd.n_sectors & 0xFF;
    fis.sector_count_high = (cmd.n_sectors >> 8) & 0xFF;

    fis.lba_0 = cmd.lba & 0xFF;
    fis.lba_1 = (cmd.lba >> 8) & 0xFF;
    fis.lba_2 = (cmd.lba >> 16) & 0xFF;
    fis.lba_3 = (cmd.lba >> 24) & 0xFF;
    fis.lba_4 = (cmd.lba >> 32) & 0xFF;
    fis.lba_5 = (cmd.lba >> 40) & 0xFF;

    fis.features = cmd.features & 0xFF;
    fis.features_exp = (cmd.features >> 8) & 0xFF;

    auto region = controller->iommu_vmm.alloc(transfer_len, cmd.write ? iovmm::Iovmm::HostToDevice : iovmm::Iovmm::DeviceToHost);
    ASSERT(region.guest_base);

    if(cmd.write)
        memcpy(region.host_base, data, transfer_len);

    for(size_t i = 0; i < n_prdts; i++) {
        auto& prdt = slot.table->prdts[i];

        size_t remaining = transfer_len - (i * 0x3FFFFF);
        size_t transfer = (remaining >= 0x3FFFFF) ? 0x3FFFFF : remaining;

        prdt.flags.byte_count = Prdt::calculate_bytecount(transfer);
        uintptr_t pa = region.guest_base + (i * 0x3FFFFF);

        prdt.low = pa & 0xFFFF'FFFF;
        if(controller->a64)
            prdt.high = (pa >> 32) & 0xFFFF'FFFF;
    }

    wait_ready();

    regs->ci |= (1 << slot.index);

    while(regs->ci & (1 << slot.index)) {
        if(regs->is & (1 << 30)) {
            if(regs->tfd & (1 << 0)) {
                auto err = regs->tfd >> 8;
                print("ahci: Error on CMD, code {}\n", err);
                return;
            }
        }
    }

    if(!cmd.write)
        memcpy(data, region.host_base, transfer_len);
    controller->iommu_vmm.free(region);

    free_command(slot);
}

void ahci::Controller::Port::send_atapi_cmd(const ata::ATAPICommand& cmd, uint8_t* data, size_t transfer_len) {
    size_t n_prdts = div_ceil(transfer_len, 0x3FFFFF);
    auto slot = allocate_command(n_prdts);
    ASSERT(slot.index != ~0);

    region->command_headers[slot.index].flags.prdtl = n_prdts;
    region->command_headers[slot.index].flags.write = cmd.write;
    region->command_headers[slot.index].flags.cfl = 5; // h2d register is 5 dwords
    region->command_headers[slot.index].flags.atapi = 1;

    auto& fis = *(H2DRegisterFIS*)slot.table->fis;
    fis.type = FISTypeH2DRegister;
    fis.flags.c = 1;

    fis.command = ata::commands::SendPacket;
    fis.features = 1; // Tell device main transfer is happining via DMA
    fis.control = 0x8;
    fis.dev_head = 0xA0;

    fis.lba_1 = transfer_len & 0xFF; // Seems to not actually be needed?
    fis.lba_2 = (transfer_len >> 8) & 0xFF;

    memcpy(slot.table->packet, cmd.packet, 16);

    auto region = controller->iommu_vmm.alloc(transfer_len, cmd.write ? iovmm::Iovmm::HostToDevice : iovmm::Iovmm::DeviceToHost);
    ASSERT(region.guest_base);

    if(cmd.write)
        memcpy(region.host_base, data, transfer_len);

    for(size_t i = 0; i < n_prdts; i++) {
        auto& prdt = slot.table->prdts[i];

        size_t remaining = transfer_len - (i * 0x3FFFFF);
        size_t transfer = (remaining >= 0x3FFFFF) ? 0x3FFFFF : remaining;

        prdt.flags.byte_count = Prdt::calculate_bytecount(transfer);
        uintptr_t pa = region.guest_base + (i * 0x3FFFFF);

        prdt.low = pa & 0xFFFF'FFFF;
        if(controller->a64)
            prdt.high = (pa >> 32) & 0xFFFF'FFFF;
    }

    wait_ready();

    regs->ci |= (1 << slot.index);

    while(regs->ci & (1 << slot.index)) {
        if(regs->is & (1 << 30)) {
            if(regs->tfd & (1 << 0)) {
                auto err = regs->tfd >> 8;
                print("ahci: Error on CMD, code {}\n", err);
                return;
            }
        }
    }

    if(!cmd.write)
        memcpy(data, region.host_base, transfer_len);
    controller->iommu_vmm.free(region);

    free_command(slot);
}

std::vector<ahci::Controller> controllers;

void ahci::init() {
    pci::Device* dev = nullptr;
    size_t i = 0;
    while((dev = pci::device_by_class(1, 6, 1, i++)) != nullptr)
        controllers.emplace_back(dev);
}