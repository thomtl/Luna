#include <Luna/drivers/net/realtek/rtl81x9.hpp>
#include <Luna/net/eth.hpp>
#include <Luna/cpu/idt.hpp>

#include <std/linked_list.hpp>

rtl81x9::Nic::Nic(pci::Device& device, uint16_t did): mm{&device}, regs{nullptr} {
    checksum_offload = net::cs_offload::ipv4 | net::cs_offload::udp;


    device.set_privileges(pci::privileges::Mmio | pci::privileges::Dma);

    auto vector = idt::allocate_vector();
    device.enable_irq(0, vector);

    idt::set_handler(vector, idt::Handler{.f = [](uint8_t, idt::Regs*, void* userptr) {
        auto& self = *(Nic*)userptr;

        self.handle_irq();
    }, .is_irq = true, .should_iret = true, .userptr = this});

    mm.push_region({0x1000, 0xFFFF'FFFF - 0x1000});

    for(size_t i = 0; i < 6; i++) {
        auto bar = device.read_bar(i);
        if(bar.base != 0 && bar.type == pci::Bar::Type::Mmio) {
            auto va = bar.base + phys_mem_map;
            vmm::get_kernel_context().map(bar.base, va, paging::mapPagePresent | paging::mapPageWrite);

            regs = (volatile Regs*)va;
            break;
        }
    }

    ASSERT(regs != nullptr);

    // Reset the controller
    regs->cr = cr::reset;
    while(regs->cr & cr::reset)
        asm("pause");

    mac.data[0] = regs->id[0];
    mac.data[1] = regs->id[1];
    mac.data[2] = regs->id[2];
    mac.data[3] = regs->id[3];
    mac.data[4] = regs->id[4];
    mac.data[5] = regs->id[5];

    print("rtl81x9: Detected controller with MAC {}\n", mac);
    
    tx_alloc = mm.alloc(sizeof(TxDescriptor) * n_descriptor_sets, iovmm::Iovmm::Bidirectional);
    rx_alloc = mm.alloc(sizeof(RxDescriptor) * n_descriptor_sets, iovmm::Iovmm::Bidirectional);

    ASSERT(tx_alloc.guest_base && tx_alloc.host_base);
    ASSERT(rx_alloc.guest_base && rx_alloc.host_base);

    tx_set_alloc = mm.alloc(sizeof(SetBuffers), iovmm::Iovmm::HostToDevice);
    rx_set_alloc = mm.alloc(sizeof(SetBuffers), iovmm::Iovmm::DeviceToHost);

    ASSERT(tx_set_alloc.guest_base && tx_set_alloc.host_base);
    ASSERT(rx_set_alloc.guest_base && rx_set_alloc.host_base);

    tx = (volatile TxDescriptor*)tx_alloc.host_base;
    rx = (volatile RxDescriptor*)rx_alloc.host_base;

    tx_set = (volatile SetBuffers*)tx_set_alloc.host_base;
    rx_set = (volatile SetBuffers*)rx_set_alloc.host_base;

    for(size_t i = 0; i < n_descriptor_sets; i++) {
        auto rx_base = rx_set_alloc.guest_base + (i * mtu);
        auto tx_base = tx_set_alloc.guest_base + (i * mtu);
    
        tx[i].flags = 0;
        tx[i].vlan = 0;
        tx[i].base_low = tx_base & 0xFFFF'FFFF;
        tx[i].base_high = (tx_base >> 32) & 0xFFFF'FFFF;

        rx[i].flags = tx_flags::own | mtu;
        rx[i].vlan = 0;
        rx[i].base_low = rx_base & 0xFFFF'FFFF;
        rx[i].base_high = (rx_base >> 32) & 0xFFFF'FFFF;
    }

    tx[n_descriptor_sets - 1].flags |= tx_flags::eor;
    rx[n_descriptor_sets - 1].flags |= tx_flags::eor;


    regs->cr9346 = cr9346::unlock_regs;

    regs->cpcr = cpcr::rx_vlan | cpcr::rx_chksum | cpcr::pci_mul_rw;
    if(did == 0x8139)
        regs->cpcr |= cpcr::tx_enable | cpcr::rx_enable;

    regs->rdsar_high = (rx_alloc.guest_base >> 32) & 0xFFFF'FFFF;
    regs->rdsar_low = rx_alloc.guest_base & 0xFFFF'FFFF;

    regs->tnpds_high = (tx_alloc.guest_base >> 32) & 0xFFFF'FFFF;
    regs->tnpds_low = tx_alloc.guest_base & 0xFFFF'FFFF;


    regs->cr = cr::tx_enable;
    regs->tcr = tcr::mxdma_unlimited | tcr::ifg_normal;
    regs->rcr = rcr::mxdma_unlimited | rcr::rxftr_none;

    regs->etthr = 0x3B;
    regs->rms = 0x1FFF;

    regs->imr = isr::rx_ok | isr::rx_err | isr::tx_ok | isr::tx_err | isr::link_change;
    auto isr = regs->isr;
    regs->isr = isr; // Clear all interrupts

    regs->mpc = 0;

    regs->cr = cr::tx_enable | cr::rx_enable;
    regs->cr9346 = cr9346::lock_regs;

    tx_index = 0;
}

bool rtl81x9::Nic::send_packet(const net::Mac& dst, uint16_t ethertype, const std::span<uint8_t>& packet, uint32_t offload) {
    if(tx[tx_index].flags & tx_flags::own) {
        regs->txpoll = txpoll::poll_normal_prio;

        return false; // Try to send any descriptors left to make space
    }

    auto* buf = (uint8_t*)tx_set->descriptor[tx_index].buf;

    net::eth::Header header{};
    memcpy(header.dst_mac, dst.data, 6);
    memcpy(header.src_mac, mac.data, 6);
    header.ethertype = bswap<uint16_t>(ethertype);

    memcpy(buf, &header, sizeof(header));
    memcpy(buf + sizeof(header), packet.data(), packet.size_bytes());


    // TODO: These flags are only for 8168b
    if(offload & net::cs_offload::ipv4)
        tx[tx_index].vlan |= tx_flags::ip_cs;

    if(offload & net::cs_offload::udp)
        tx[tx_index].vlan |= tx_flags::udp_cs;

    tx[tx_index].flags |= (tx_flags::own | tx_flags::fs | tx_flags::ls | ((sizeof(header) + packet.size_bytes()) & 0xFFFF));

    tx_index = (tx_index + 1) % n_descriptor_sets;

    regs->txpoll = txpoll::poll_normal_prio; // TODO: 8136 C+ mode uses different poll reg
    return true;
}

void rtl81x9::Nic::handle_irq() {
    auto sts = regs->isr;

    if(sts & isr::link_change) {
        // TODO: Handle Link Change events
    } else if(sts & isr::tx_ok) {
        handle_tx_ok();
    } else {
        print("rtl81x9: Unhandled IRQ: {:#x}\n", sts);
    }

    regs->isr = sts;
}

void rtl81x9::Nic::handle_tx_ok() {
    for(size_t i = 0; i < n_descriptor_sets; i++) {
        if(tx[i].flags & tx_flags::own) // Descriptor was not transmitted?
            break;

        tx[i].flags &= tx_flags::eor; // Remove all info except EOR
        tx[i].vlan = 0;
    }
}

static void init(pci::Device& dev) {
    auto* nic = new rtl81x9::Nic{dev, dev.read<uint16_t>(2)};
    ASSERT(nic);

    net::register_nic(nic);
}

static std::pair<uint16_t, uint16_t> known_cards[] = {
    {0x10EC, 0x8168}
};

static pci::Driver driver = {
    .name = "RTL81x9 Family NIC Driver",
    .init = init,

    .match = pci::match::vendor_device,
    .id_list = {known_cards}
};
DECLARE_PCI_DRIVER(driver);