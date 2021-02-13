#include <Luna/drivers/usb/xhci/xhci.hpp>

#include <Luna/cpu/idt.hpp>

#include <Luna/drivers/hpet.hpp>
#include <Luna/misc/log.hpp>

template<typename F>
bool timeout(uint64_t ms, F f) {
    while(!f()) {
        ms--;
        if(ms == 0)
            return false;

        hpet::poll_sleep(1);
    }

    return true;
}

xhci::HCI::HCI(pci::Device& dev): device{&dev}, mm{&dev} {
    dev.set_privileges(pci::privileges::Mmio | pci::privileges::Dma);

    auto bar = dev.read_bar(0);

    auto va = bar.base + phys_mem_map;

    cap = (CapabilityRegs*)va;
    op = (OperationalRegs*)(va + (cap->caplength & 0xFF));
    run = (RuntimeRegs*)(va + (cap->rtsoff & ~0x1F));
    db = (uint32_t*)(va + (cap->dboff & ~0x3));


    uint16_t vendor_id = dev.read<uint16_t>(0);
    if(vendor_id == 0x8086) {
        quirks |= quirkIntel;
        intel_enable_ports();
    }

    dev.write<uint8_t>(0x61, 0x20); // Write to FLADJ

    print("xhci: Detected HCI {}.{}.{}\n", (cap->hci_version >> 8) & 0xFF, (cap->hci_version >> 4) & 0xF, cap->hci_version & 0xF);

    n_slots = cap->hcsparams1 & 0xFF;
    n_interrupters = (cap->hcsparams1 >> 8) & 0x7FF;
    n_ports = (cap->hcsparams1 >> 24) & 0xFF;
    
    n_scratchbufs = ((cap->hcsparams2 >> 16) & 0x3E0) | ((cap->hcsparams2 >> 27) & 0x1F);
    context_size = ((cap->hccparams1 >> 2) & 1) ? 64 : 32;

    port_power_control = (cap->hccparams1 >> 3) & 1;

    auto a64 = (cap->hccparams1 & 1);
    if(a64)
        mm.push_region({0x1000, 0xFFFF'FFFF'FFFF'FFFF - 0x1000});
    else
        mm.push_region({0x1000, 0xFFFF'FFFF - 0x1000});

    {
        uint8_t bit = 0;
        for(uint8_t i = 0; i < 16; i++) {
            if(op->pagesize & (1 << i)) {
                bit = i;
                break;
            }
        }

        page_size = 1 << (bit + 12);
    }

    {
        uint16_t off = (cap->hccparams1 >> 16) & 0xFFFF;
        ext_caps = (volatile uint32_t*)(va + (off * 4));

        size_t i = 0;
        while(true) {
            auto id = ext_caps[i] & 0xFF;
            auto off = (ext_caps[i] >> 8) & 0xFF;

            if(id == 1) {
                // BIOS-OS handoff was already done
            } else if(id == 2) {
                auto* item = (volatile ProtocolCap*)&ext_caps[i];

                auto& proto = protocols.emplace_back();

                proto.major = (item->version >> 24) & 0xFF;
                proto.minor = (item->version >> 16) & 0xFF;

                proto.port_count = (item->ports_info >> 8) & 0xFF;
                proto.port_off = (item->ports_info & 0xFF) - 1;

                proto.slot_type = item->slot_type & 0x1F;

                print("      Protocol: {}{}{}{}{}.{}\n", (char)item->name, (char)(item->name >> 8), (char)(item->name >> 16), (char)(item->name >> 24), (uint16_t)proto.major, (uint16_t)proto.minor);
            } else if(id >= 192) {
                // Vendor Specific, so ignore
            } else {
                print("      Unhandled Extended Cap with id {}\n", id);
            }

            if(off == 0)
                break;
            
            i += off;
        }
    }


    reset_controller();

    dcbaap_alloc = mm.alloc(sizeof(uint64_t) * (n_ports + 2), iovmm::Iovmm::HostToDevice);
    dcbaap = (uint64_t*)dcbaap_alloc.host_base;

    if(n_scratchbufs > 0) {
        auto alloc = mm.alloc(sizeof(uint64_t) * n_scratchbufs, iovmm::Iovmm::HostToDevice);
        dcbaap[0] = alloc.guest_base;

        auto* scratch_index = (volatile uint64_t*)alloc.host_base;

        for(size_t i = 0; i < n_scratchbufs; i++) {
            auto scratch = mm.alloc(page_size, iovmm::Iovmm::Bidirectional);
            scratch_index[i] = scratch.guest_base;
        }
    }

    op->config = n_slots;

    op->dcbaap = dcbaap_alloc.guest_base;
    
    cmd_ring.init(mm, 256ull, &db[0]);
    evt_ring.init(mm, 256ull, RingType::Event);

    op->crcr = (op->crcr & 0x3F) | cmd_ring->get_guest_base() | cmd_ring->get_cycle();

    {
        auto vector = idt::allocate_vector();
        dev.enable_irq(0, vector);

        idt::set_handler(vector, idt::handler{.f = [](uint8_t, idt::regs*, void* userptr){
            auto& self = *(HCI*)userptr;
            self.handle_irq();
        }, .is_irq = true, .should_iret = true, .userptr = this});

        auto alloc = mm.alloc(sizeof(ERSTEntry), iovmm::Iovmm::HostToDevice);
        erst = (volatile ERSTEntry*)alloc.host_base;

        erst->ring_base = evt_ring->get_guest_base();
        erst->ring_size = 256;

        run->interrupters[0].iman = iman::irq_pending | iman::irq_enable;
        (void)run->interrupters[0].iman;
        run->interrupters[0].imod = 0; // Disable IRQ Throttling

        run->interrupters[0].erst_size = 1;
        run->interrupters[0].erst_dequeue = evt_ring->get_guest_base() | (1 << 3);
        run->interrupters[0].erst_base = alloc.guest_base;
    }
    op->usbsts |= (1 << 10) | (1 << 4) | (1 << 3) | (1 << 2);
    op->usbcmd |= (usbcmd::run | usbcmd::irq_enable);

    ASSERT(timeout(20, [&]{ return (op->usbsts & usbsts::halted) == 0; }));

    print("      Enabled xHCI\n");

    enumerate_ports();
}

void xhci::HCI::enumerate_ports() {
    ports.resize(n_ports);

    size_t usb3_i = 0, usb2_i = 0;
    for(auto& proto : protocols) {
        if(proto.major == 2) {
            for(size_t i = 0; i < proto.port_count; i++) {
                ports[proto.port_off + i].port_id = proto.port_off + i;
                ports[proto.port_off + i].proto = &proto;
                ports[proto.port_off + i].offset = usb2_i++;
            }
        } else if(proto.major == 3) {
            for(size_t i = 0; i < proto.port_count; i++) {
                ports[proto.port_off + i].port_id = proto.port_off + i;
                ports[proto.port_off + i].proto = &proto;
                ports[proto.port_off + i].offset = usb3_i++;
            }
        } else {
            print("xhci: Unknown Protocol Major Version {}\n", (uint16_t)proto.major);
        }
    }

    for(size_t i = 0; i < ports.size(); i++) {
        for(size_t j = 0; j < ports.size(); j++) {
            if((ports[i].offset == ports[j].offset) && (ports[i].proto != ports[j].proto)) {
                ports[i].has_pair = true;
                ports[i].other_port = j;

                ports[j].has_pair = true;
                ports[j].other_port = i;
            }
        }
    }

    // Mark all ports active, in the case of a paired port prefer USB3
    for(auto& port : ports)
        if(port.proto)
            if(port.proto->major == 3 || (port.proto->major == 2 && !port.has_pair))
                port.active = true;

    // Try to reset all USB3 ports, if that fails for any paired ports retry with USB2
    for(auto& port : ports)
        if(port.proto)
            if(port.proto->major == 3 && port.active)
                port.active = reset_port(port);

    for(auto& port : ports)
        if(port.proto)
            if(port.proto->major == 2 && port.active)
                port.active = reset_port(port);

    for(auto& port : ports) {
        if(!port.active || !port.proto)
            continue;

        port.hci = this;

        port.speed = (op->ports[port.port_id].portsc >> 10) & 0xF;
        if(port.speed == 0) // Nothing attached i guess
            continue;

        if(port.speed > portsc::super_speed) {
            print("      Unknown speed: {}\n", (uint16_t)port.speed);
            continue;
        }

        size_t packet_size[] = {
            [0] = 0,
            [portsc::full_speed] = 64, // Default size, we should query it though
            [portsc::low_speed] = 8,
            [portsc::high_speed] = 64,
            [portsc::super_speed] = 512
        };
        port.max_packet_size = packet_size[port.speed];

        {
            TRBCmdEnableSlot cmd{};
            cmd.type = trb_types::enable_slot_cmd;
            cmd.slot_type = port.proto->slot_type;
            auto res = cmd_ring->issue(cmd);
            if(auto code = res.code; code != trb_codes::success) {
                print("      Failed to Enable Slot, Code: {}\n", code);
                continue;
            }

            port.slot_id = res.slot_id;
            port_by_slot[port.slot_id] = &port;
        }

        port.dev_ctx.init(mm, context_size);
        port.in_ctx.init(mm, context_size);
        port.ep0_queue.init(mm, 256ull, &db[port.slot_id]);

        dcbaap[port.slot_id] = port.dev_ctx->get_guest_base();

        {
            auto& in = port.in_ctx->get_in_ctx();

            in.add_flags = 0b11;
            in.drop_flags = 0;
        }

        {
            auto& slot = port.in_ctx->get_slot_ctx();

            slot.context_entries = 1; // Only EP0 is setup by default
            slot.speed = port.speed;
            slot.interrupter = 0;
            slot.root_hub_port_num = port.port_id + 1;
        }

        {
            auto& ep0 = port.in_ctx->get_ep0_ctx();

            ep0.ep_type = ep_types::control_bi;
            ep0.error_count = 3;
            ep0.max_burst_size = 0;
            ep0.max_packet_size = port.max_packet_size;
            ep0.average_trb_len = 8; // All Control Packets are 8 bytes
            
            ep0.tr_dequeue = port.ep0_queue->get_guest_base() | port.ep0_queue->get_cycle();
        }

        TRBCmdAddressDevice addr_dev{};
        {
            addr_dev.type = trb_types::address_device_cmd;
            addr_dev.bsr = 0; // Don't send ADDRESS_DEVICE packet yet
            addr_dev.input_ctx = port.in_ctx->get_guest_base();
            addr_dev.slot_id = port.slot_id;
            auto res = cmd_ring->issue(addr_dev);
            if(auto code = res.code; code != trb_codes::success) {
                print("      Failed to Address Device, Code: {}\n", code);
                continue;
            }
        }

        usb::spec::DeviceRequestPacket packet{};
        packet.type = usb::spec::request_type::device_to_host | usb::spec::request_type::to_standard | usb::spec::request_type::device;
        packet.request = usb::spec::request_ops::get_descriptor;
        packet.value = (1 << 8);
        packet.length = 8;

        usb::spec::DeviceDescriptor desc{};
        send_ep0_control(port, packet, false, 8, (uint8_t*)&desc);

        //ASSERT(reset_port(port));

        size_t max_packet = 0;
        if((desc.usb_version >> 8) == 3) // USB3 Defines it as 2^n, USB2 just as n
            max_packet = (1 << desc.max_packet_size);
        else
            max_packet = desc.max_packet_size;
        
        if(port.max_packet_size != max_packet) {
            port.max_packet_size = max_packet;
            port.in_ctx->get_ep0_ctx().max_packet_size = max_packet;

            TRBCmdAddressDevice eval{};
            eval.type = trb_types::evaluate_context_cmd;
            eval.input_ctx = port.in_ctx->get_guest_base();
            eval.slot_id = port.slot_id;
            auto res = cmd_ring->issue(eval);
            if(auto code = res.code; code != trb_codes::success) {
                print("      Failed to Evaluate Context, Code: {}\n", code);
                continue;
            }
        }

        /*{
            // Update any things that might've changed in the Device Context
            port.ep0_queue->reset();

            addr_dev.bsr = 0; // Actually send ADDRESS_DEVICE this time
            auto res = cmd_ring->issue(addr_dev);
            if(auto code = res.code; code != trb_codes::success) {
                print("      Failed to Address Device, Code: {}\n", code);
                continue;
            }
        }*/

        usb::DeviceDriver driver{};
        driver.addressed = true; // xHCI sends the ADDRESS_DEVICE packet for us
        driver.userptr = &port;
        driver.setup_ep = +[](void* userptr, const usb::EndpointData& ep) -> bool {
            auto& port = *(Port*)userptr;
            return port.hci->setup_ep(port, ep);
        };

        driver.ep0_control_xfer = +[](void* userptr, const usb::ControlXfer& xfer) -> bool {
            auto& port = *(Port*)userptr;
            return port.hci->send_ep0_control(port, xfer.packet, xfer.write, xfer.len, xfer.buf);
        };

        driver.ep_bulk_xfer = +[](void* userptr, uint8_t epid, std::span<uint8_t> data) -> bool {
            auto& port = *(Port*)userptr;
            return port.hci->send_ep_bulk(port, epid, data);
        };

        usb::register_device(driver);
    }
}

bool xhci::HCI::send_ep0_control(Port& port, const usb::spec::DeviceRequestPacket& packet, bool write, size_t len, uint8_t* buf) {
    uint8_t type = 0;
    if(len > 0 && write) type = 2; // OUT Data Stage
    else if(len > 0 && !write) type = 3; // IN Data Stage
    else type = 0; // No Data Stage

    TRBSetup setup{};
    setup.type = trb_types::setup;
    setup.ioc = 0;
    setup.len = 8;
    setup.immediate = 1;
    setup.transfer_type = type;

    setup.bmRequestType = packet.type;
    setup.bType = packet.request;
    setup.wValue = packet.value;
    setup.wIndex = packet.index;
    setup.wLength = packet.length;
    port.ep0_queue->enqueue(setup);

    iovmm::Iovmm::Allocation dma{};
    bool allocated = false;
    if(len > 0) {
        TRBData data{};
        data.type = trb_types::data;
        data.chain = 0;
        data.ioc = 0;
        data.td_size = 0;
        data.direction = write ? 0 : 1;

        if(len <= 8 && write) {
            memcpy((uint8_t*)&data.buf, buf, len);

            data.immediate_data = 1;
            data.len = len;
        } else {
            dma = mm.alloc(len, write ? iovmm::Iovmm::HostToDevice : iovmm::Iovmm::DeviceToHost);
            allocated = true;
            if(write)
                memcpy(dma.host_base, buf, len);
                
            data.buf = dma.guest_base;
            data.len = len;
        }

        port.ep0_queue->enqueue(data);
    }

    TRBStatus status{};
    status.type = trb_types::status;
    status.ioc = 1;
    status.direction = (!write && len > 0) ? 0 : 1;
    auto i = port.ep0_queue->enqueue(status);
    
    auto evt = port.ep0_queue->run(i, 1);
    if(evt.code != trb_codes::success) {
        print("xhci: Failed to do EP0 Control Transfer\n");
        return false;
    }

    if(!write)
        memcpy(buf, dma.host_base, len);

    if(allocated)
        mm.free(dma);

    return true;
}

bool xhci::HCI::send_ep_bulk(xhci::HCI::Port& port, uint8_t epid, std::span<uint8_t> data) {
    auto& ring = *port.ep_rings[epid].second;
    auto& ep = port.ep_rings[epid].first;
    bool write = !ep.desc.dir;

    TRBNormal xfer{};
    xfer.type = trb_types::normal;
    xfer.ioc = 1;
    xfer.len = data.size_bytes();

    iovmm::Iovmm::Allocation dma{};
    bool allocated = false;
    if(data.size_bytes() <= 8 && write) { // Eligible for Immediate Data
        xfer.immediate_data = 1;
        memcpy((uint8_t*)&xfer.data_buf, data.data(), data.size_bytes());
    } else {
        dma = mm.alloc(data.size_bytes(), write ? iovmm::Iovmm::HostToDevice : iovmm::Iovmm::DeviceToHost);
        allocated = true;

        if(write)
            memcpy(dma.host_base, data.data(), data.size_bytes());

        xfer.data_buf = dma.guest_base;
    }

    auto i = ring.enqueue(xfer);
    auto res = ring.run(i, epid);
    if(auto code = res.code; code != trb_codes::success && code != trb_codes::short_packet) {
        print("xhci: Failed to do EP Bulk Transfer, code: {}\n", code);
        return false;
    }

    if(!write)
        memcpy(data.data(), dma.host_base, data.size_bytes());

    if(allocated)
        mm.free(dma);

    return true;
}

bool xhci::HCI::setup_ep(xhci::HCI::Port& port, const usb::EndpointData& ep) {
    uint8_t n = (ep.desc.ep_num * 2) + (ep.desc.dir ? 1 : 0);

    auto& in = port.in_ctx->get_in_ctx();
    in.add_flags = 1 | (1 << n);
    in.drop_flags = 0;

    auto& slot = port.in_ctx->get_slot_ctx();
    memcpy((uint8_t*)&slot, (uint8_t*)&port.dev_ctx->get_slot_ctx(), context_size);
    slot.context_entries = n + 1;

    auto& ctx = port.in_ctx->get_ep_ctx(ep.desc.ep_num, ep.desc.dir);

    ctx.error_count = 3;

    ctx.max_primary_streams = 0;
    ctx.linear_stream_array = 0;
    ctx.host_initiate_disable = 0;

    ctx.max_packet_size = ep.desc.max_packet_size;
    if(port.proto->major >= 3)
        ctx.max_burst_size = ep.companion.max_burst;
    if(port.proto->major >= 3 && ep.desc.ep_type == usb::spec::ep_type::isoch)
        ctx.mult = ep.companion.attributes & 0b11;
    ctx.average_trb_len = 0;//ep.max_packet_size * 2;

    ctx.interval = 0; // 0 for SS Bulk EPs

    ctx.max_esit_low = 0;
    ctx.max_esit_high = 0;

    if(ep.desc.ep_type == usb::spec::ep_type::bulk)
        ctx.ep_type = ep.desc.dir ? ep_types::bulk_in : ep_types::bulk_out;
    else if(ep.desc.ep_type == usb::spec::ep_type::irq)
        ctx.ep_type = ep.desc.dir ? ep_types::interrupt_in : ep_types::interrupt_out;
    else if(ep.desc.ep_type == usb::spec::ep_type::isoch)
        ctx.ep_type = ep.desc.dir ? ep_types::isoch_in : ep_types::isoch_out;
    else if(ep.desc.ep_type == usb::spec::ep_type::control)
        ctx.ep_type = ep_types::control_bi;
    else
        PANIC("Unknown EP Type");


    

    auto* ring = new TransferRing{mm, 256, &db[port.slot_id]};
    ctx.tr_dequeue = ring->get_guest_base() | ring->get_cycle();

    port.ep_rings[n] = {ep, ring};

    TRBCmdConfigureEP cmd{};
    cmd.type = trb_types::configure_ep_cmd;
    cmd.dc = 0;
    cmd.slot_id = port.slot_id;
    cmd.input_ctx = port.in_ctx->get_guest_base();

    const auto res = cmd_ring->issue(cmd);
    if(res.code != trb_codes::success) {
        print("xhci: Failed to configure EP: {}\n", res.code);
        return false;
    }

    return true;
}

void xhci::HCI::intel_enable_ports() {
    auto subsys_vid = device->read<uint16_t>(0x2C);
    auto subsys_did = device->read<uint16_t>(0x2E);

    if(subsys_vid == 0x104D && subsys_did == 0x90A8) // Certain Sony VAIO laptops don't support this
        return;

    device->write<uint32_t>(0xD8, device->read<uint32_t>(0xDC));
    device->read<uint32_t>(0xD8);

    device->write<uint32_t>(0xD0, device->read<uint32_t>(0xD4));
    device->read<uint32_t>(0xD0);
}

void xhci::HCI::reset_controller() {
    op->usbcmd &= ~(usbcmd::run | usbcmd::irq_enable); // Stop controller

    if(quirks & quirkIntel)
        hpet::poll_sleep(2); // Intel controllers need some time here

    ASSERT(timeout(20, [&]{ return (op->usbsts & usbsts::halted) != 0; }));

    op->usbcmd |= usbcmd::reset;
    while(op->usbcmd & usbcmd::reset || op->usbsts & usbsts::not_ready)
        asm("pause");
    hpet::poll_sleep(10); // Recovery time
}

bool xhci::HCI::reset_port(Port& port) {
    auto& reg = op->ports[port.port_id];
    if((reg.portsc & portsc::port_power) == 0) {
        reg.portsc = portsc::port_power;

        ASSERT(timeout(20, [&]{ return (reg.portsc & portsc::port_power) != 0; }));
    }
        
    reg.portsc = portsc::port_power | portsc::status_change_bits;

    bool ccs = (reg.portsc & portsc::connect_status);
    if(!ccs) {
        // Reset failed, if paired port try USB2
        if(port.has_pair) {
            port.active = false;
            ports[port.other_port].active = true;
        }

        return false;
    }

    auto reset_bit = (port.proto->major == 3) ? portsc::warm_reset : portsc::reset;
    auto reset_change_bit = portsc::reset_change;//(port.proto->major == 3) ? portsc::warm_port_reset_change : portsc::reset_change;
    reg.portsc = portsc::port_power | reset_bit;

    if(timeout(500, [&]{ return (reg.portsc & reset_change_bit) != 0; })) {
        hpet::poll_sleep(3); // Recovery time

        if(reg.portsc & portsc::enabled) {
            reg.portsc = portsc::port_power | portsc::status_change_bits;
            return true;
        }
    }

    // Reset failed, if paired port try USB2
    if(port.has_pair) {
        port.active = false;
        ports[port.other_port].active = true;
    }

    return false;
}

void xhci::HCI::handle_irq() {
    auto sts = op->usbsts;
    if(!(sts & usbsts::irq))
        return;
    
    if(sts & usbsts::halted) {
        print("xhci: HC Halted\n");
    }

    if(sts & usbsts::host_system_error) {
        print("xhci: Host System Error\n");
    }
    
    op->usbsts |= usbsts::irq; // ACK IRQ

    if(run->interrupters[0].iman & iman::irq_pending) {
        run->interrupters[0].iman |= iman::irq_pending; // Clear Interrupter Pending
        (void)run->interrupters[0].iman;
    }

    if(run->interrupters[0].erst_dequeue & (1 << 3)) {
        auto* trb = evt_ring->peek_dequeue();
        while(trb->cycle == evt_ring->get_cycle()) {
            auto type = trb->type;

            if(type == trb_types::cmd_completion_evt) {
                auto* evt = (TRBCmdCompletionEvt*)trb;

                size_t i = (evt->cmd_ptr - cmd_ring->get_guest_base()) / 16;
                cmd_ring->complete(i, *evt);
            } else if(type == trb_types::transfer_evt) {
                auto* evt = (TRBXferCompletionEvt*)trb;

                TransferRing* ring = nullptr;
                if(evt->epid == 1) { // EP0
                    ring = port_by_slot[evt->slot_id]->ep0_queue.get();
                } else {
                    ring = port_by_slot[evt->slot_id]->ep_rings[evt->epid].second;
                }

                // TODO: Error handling on EP0
                if(auto code = evt->code; evt->epid == 1 && code != trb_codes::success) {
                    print("xhci: Transfer Event Error: {}\n", code);

                    auto ptr = evt->cmd_ptr;
                    auto slot = evt->slot_id;
                    auto epid = evt->epid;

                    print("      Slot: {}, EPID: {}\n", slot, epid);
                    print("      Ptr: {:#x}\n", ptr);
                } else {
                    size_t i = (evt->cmd_ptr - ring->get_guest_base()) / 16;
                    ring->complete(i, *evt);
                }
            } else if(type == trb_types::status_change_evt) {
                // Currently unhandled
                // TODO: Detect port hotplug
            } else {
                print("xhci: Unknown event: {}\n", type);
            }

            trb = evt_ring->dequeue();
            if(evt_ring->get_dequeue_ptr() == 0) {
                evt_ring->get_cycle() ^= 1; // Just wrapped around
            }
        }

        auto dequeue_ptr = evt_ring->get_guest_base() + (evt_ring->get_dequeue_ptr() * 16);
        run->interrupters[0].erst_dequeue = dequeue_ptr | (1 << 3); // Advance Dequeue ptr, and clear busy
    }

    //print("end\n");
}

static void handoff_bios(pci::Device& dev) {
    auto va = dev.read_bar(0).base + phys_mem_map;
    auto* cap = (xhci::CapabilityRegs*)va;

    uint16_t off = (cap->hccparams1 >> 16) & 0xFFFF;
    auto* ext_caps = (volatile uint32_t*)(va + (off * 4));

    size_t i = 0;
    while(true) {
        auto id = ext_caps[i] & 0xFF;
        auto off = (ext_caps[i] >> 8) & 0xFF;

        if(id == 1) {
            auto* item = (volatile xhci::LegacyCap*)&ext_caps[i];

            if(item->usblegsup & xhci::usblegsup::bios_owned) {
                print("xhci: Doing BIOS <=> OS Handoff ... ");

                item->usblegsup |= xhci::usblegsup::os_owned;
                if(timeout(1000, [&] { return (item->usblegsup & xhci::usblegsup::bios_owned) == 0; }))
                    print("Done\n");
                else
                    print("BIOS failed to release ownership, trying anyway\n");
            }

            item->usblegctlsts &= ~1;
        }

        if(off == 0)
            break;
            
        i += off;
    }
}

static void init(pci::Device& dev) {
    new xhci::HCI{dev};
}

static pci::Driver driver = {
    .name = "xHCI Driver",
    .bios_handoff = handoff_bios,
    .init = init,

    .match = pci::match::class_code | pci::match::subclass_code | pci::match::protocol_code,
    .class_code = 0xC,
    .subclass_code = 0x3,
    .protocol_code = 0x30
};
DECLARE_PCI_DRIVER(driver);