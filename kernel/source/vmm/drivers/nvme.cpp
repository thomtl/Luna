#include <Luna/vmm/drivers/nvme.hpp>
#include <Luna/misc/uuid.hpp>

using namespace vm::nvme;

Driver::Driver(Vm* vm, pci::HostBridge* bridge, uint8_t slot, uint8_t func, vfs::File* file): PCIDriver{vm}, vm{vm}, file{file} {
    bridge->register_pci_driver(pci::DeviceID{0, 0, slot, func}, this);

    pci_space->header.vendor_id = 0x8086;
    pci_space->header.device_id = 0xF1A5; // Intel SSD 600P Series
    pci_space->header.revision = 3;

    pci_space->header.subsystem_vendor_id = 0x8086;
    pci_space->header.subsystem_device_id = 0x390A;

    pci_space->header.class_id = 1; // Storage
    pci_space->header.subclass = 8; // NVMe
    pci_space->header.prog_if = 2; // NVMe IO Controller

    pci_space->header.irq_pin = 1;

    if(func > 0)
        pci_space->header.header_type = 0x80;

    pci_init_bar(0, bar_size, true, true); // MMIO, 64bit

    queues[0].send_irqs = true;
}

void Driver::mmio_write(uintptr_t addr, uint64_t value, uint8_t size) {
    auto reg = addr - mmio_base;

    if(reg == regs::cc && size == 4) {
        if((cc & regs::cc_en) && !(value & regs::cc_en)) { // On to Off
            csts &= ~regs::csts_rdy;

            auto admin = queues[0];
            queues.clear();

            // ASQ, ACQ, and AQA are preserved
            queues[0].sq_base = admin.sq_base;
            queues[0].cq_base = admin.cq_base;
            queues[0].sqs = admin.sqs;
            queues[0].cqs = admin.cqs;
            queues[0].send_irqs = admin.send_irqs;
        }
                
        if(!(cc & regs::cc_en) && (value & regs::cc_en)) // Off to On
            csts |= regs::csts_rdy;

        ASSERT(((value >> 4) & 7) == 0); // NVM Command Set not selected
        ASSERT(((value >> 7) & 0xF) == 0); // Pagesize != 4KiB
        ASSERT(((value >> 11) & 7) == 0); // Not Round Robin

        sq_entry_size = 1 << ((value >> 16) & 0xF);
        cq_entry_size = 1 << ((value >> 20) & 0xF);

        cc = value;
    } else if(reg == regs::intms && size == 4) {
        irq_mask |= value; // Write 1 = set write 0 = no effect
        check_irq();
    } else if(reg == regs::intmc && size == 4) {
        irq_mask &= ~value; // Write 1 = clear, 0 = no effect
        check_irq();
    } else if(reg == regs::aqa && size == 4) {
        queues[0].cqs = ((value >> 16) & 0xFFF) + 1;
        queues[0].sqs = (value & 0xFFF) + 1;

        ASSERT(queues[0].cqs <= max_queue_entries);
        ASSERT(queues[0].sqs <= max_queue_entries);
    } else if(reg == regs::asq) {
        queues[0].sq_base = value;

        ASSERT((queues[0].sq_base & 0xFFF) == 0);
    } else if(reg == (regs::asq + 4) && size == 4) {
        queues[0].sq_base &= ~0xFFFF'FFFF'0000'0000;
        queues[0].sq_base |= (value << 32);
    } else if(reg == regs::acq) {
        queues[0].cq_base = value;

        ASSERT((queues[0].cq_base & 0xFFF) == 0);
    } else if(reg == (regs::acq + 4) && size == 4) {
        queues[0].cq_base &= ~0xFFFF'FFFF'0000'0000;
        queues[0].cq_base |= (value << 32);
    } else if((reg & ~0xFFF) >= 1) {
        auto db = reg - 0x1000;

        db /= 4;

        auto qid = (db & ~1) / 2;
        bool completion = db & 1;

        if(!completion)
            kick_queue(qid, value);
        else {
            queues[qid].cq_head = value;

            if(queues[qid].cq_head == queues[qid].cq_tail && queues[qid].send_irqs)
                update_irqs(qid, false);
        }
    } else {
        print("nvme: Unknown MMIO write {:#x} <- {:#x} ({})\n", reg, value, size);
        PANIC("Unknown reg");
    }
}

uint64_t Driver::mmio_read(uintptr_t addr, uint8_t size) {
    auto reg = addr - mmio_base;

    if(reg == regs::cap_low)
        return cap;
    else if(reg == regs::cap_high && size == 4)
        return cap >> 32;
    else if(reg == regs::version && size == 4)
        return (1 << 16) | (4 << 8) | 0; // 1.4.0
    else if(reg == regs::csts && size == 4)
        return csts;
    else if(reg == regs::cc && size == 4) 
        return cc;
    else if(reg == regs::aqa)
        return (((queues[0].cqs + 1) & 0xFFF) << 16) | ((queues[0].sqs + 1) & 0xFFF);
    else if(reg == regs::asq)
        return queues[0].sq_base;
    else if(reg == (regs::asq + 4) && size == 4)
        return queues[0].sq_base >> 32;
    else if(reg == regs::acq)
        return queues[0].cq_base;
    else if(reg == (regs::acq + 4) && size == 4)
        return queues[0].cq_base >> 32;
    else if(reg == (regs::cmbsz) && size == 4)
        return 0; // Feature unsupported in CAP
    else {
        print("nvme: Unknown MMIO read from {:#x}, size: {}\n", reg, size);
        PANIC("Unknown reg");
    }
            
    return 0;
}

void Driver::kick_queue(uint16_t qid, uint32_t value) {
    auto& queue = queues[qid];
    queue.sq_tail = value;

    auto* cmd_data = new uint8_t[sq_entry_size];
    auto& cmd = *(SubmissionEntry*)cmd_data;

    while(queue.sq_head != queue.sq_tail) {
        vm->cpus[0].dma_read(queue.sq_base + (queue.sq_head * sq_entry_size), {cmd_data, sq_entry_size});

        auto next_head = (queue.sq_head + 1) % queue.sqs;                    
                
        auto res = qid == 0 ? admin_queue_handle(cmd) : nvm_queue_handle(cmd);
        res.cid = cmd.cid;
        res.sq_id = qid;
        res.sq_head = next_head;
        cq_push(qid, res);

        queue.sq_head = next_head;
    }

    delete[] cmd_data;
}

CompletionEntry Driver::admin_queue_handle(const SubmissionEntry& cmd) {
    CompletionEntry c{};

    auto opcode = cmd.opcode;
    if(opcode == 1) { // Create IO Submission Queue
        ASSERT(cmd.cmd_data[1] & (1 << 0)); // Physically Contiguous

        auto qid = cmd.cmd_data[0] & 0xFFFF;
        auto size = (cmd.cmd_data[0] >> 16) + 1;

        auto cqid = cmd.cmd_data[1] >> 16;

        ASSERT(qid == cqid);

        if(!queues.contains(cqid)) {
            c.status = (1 << 8) | 0;
        } else if(qid == 0) {
            c.status = (1 << 8) | 1;
        } else if(size == 1 || cq_entry_size == 0) { // Size cannot be 0, and minimum is 2
            c.status = (1 << 8) | 2;
        } else {
            auto& queue = queues[qid];

            queue.sq_base = cmd.prp0;
            queue.sqs = size;

            c.status = 0;
        }
    } else if(opcode == 5) { // Create IO Completion Queue
        ASSERT(cmd.cmd_data[1] & (1 << 0)); // Physically Contiguous
        bool send_irqs = (cmd.cmd_data[1] >> 1) & 1;

        auto qid = cmd.cmd_data[0] & 0xFFFF;
        auto size = (cmd.cmd_data[0] >> 16) + 1;
                
        if(qid == 0 || queues.contains(qid)) {
            c.status = (1 << 8) | 1;
        } else if(size == 0 || cq_entry_size == 0) {
            c.status = (1 << 8) | 2;
        } else {
            Queue queue{};
            queue.cq_base = cmd.prp0;
            queue.cqs = size;
            queue.send_irqs = send_irqs;

            queues[qid] = queue;

            c.status = 0;
        }
    } else if(opcode == 6) { // Identify
        c.status = handle_admin_identify(cmd) ? 0 : 0xB;
    } else if(opcode == 9) { // Set Features
        auto fid = cmd.cmd_data[0] & 0xFF;
        print("nvme: Setting unknown Feature: {:#x}\n", fid);

        c.status = 0;
    } else {
        print("nvme: Unknown Admin Opcode: {}\n", opcode);
        PANIC("Unknown op");
    }

    return c;
}

CompletionEntry Driver::nvm_queue_handle(const SubmissionEntry& cmd) {
    CompletionEntry c{};

    auto opcode = cmd.opcode;
    if(opcode == 1) { // Write
        print("nvme: TODO: Don't ignore writes\n");
        c.status = 0;
    } else if(opcode == 2) { // Read
        auto lba = cmd.cmd_data[0] | ((uint64_t)cmd.cmd_data[1] << 32);
        auto n_lbas = (cmd.cmd_data[2] & 0xFFFF) + 1;

        ASSERT(cmd.prp == 0); // Use PRPs

        uint64_t dst = cmd.prp0;

        uint64_t prp_list = cmd.prp1;
        uint64_t prp_i = 0;

        auto* buf = new uint8_t[512];

        size_t count = 0;
        while(count != n_lbas) {
            file->read((lba + count) * 512, 512, buf);

            vm->cpus[0].dma_write(dst, {buf, 512});
            count++;

            if((count % 8) == 0) { // 8 Sectors in 1 page
                if(n_lbas > 8 && n_lbas <= 16)
                    dst = cmd.prp1;
                else
                    vm->cpus[0].dma_read(prp_list + (prp_i * 8), {(uint8_t*)&dst, 8});
                prp_i++;
            } else {
                dst += 512;
            }
        }

        delete[] buf;

        c.status = 0;
    } else {
        print("nvme: Unknown NVM Command {}\n", opcode);
        PANIC("Unknown cmd");
    }

    return c;
}

void Driver::cq_push(uint16_t qid, CompletionEntry entry) {
    auto& queue = queues[qid];
    entry.phase = queue.phase;

    vm->cpus[0].dma_write(queue.cq_base + (queue.cq_tail * cq_entry_size), {(uint8_t*)&entry, cq_entry_size});
    queue.cq_tail = (queue.cq_tail + 1) % queue.cqs;

    if(queue.cq_tail == 0) // Just wrapped around
        queue.phase = !queue.phase;

    if(queue.cq_tail != queue.cq_head && queue.send_irqs) {
        update_irqs(qid, true);
    }
}

bool Driver::handle_admin_identify(const SubmissionEntry& cmd) {
    auto cns = cmd.cmd_data[0] & 0xFF;
    auto csi = (cmd.cmd_data[1] >> 24) & 0xFF;

    if(cns == 0) {
        if(cmd.nsid != 1)
            return false;

        NamespaceIdentify data{};
        auto blocks = file->get_size() / 512;
        data.nsze = blocks;
        data.ncap = blocks;
        data.nuse = blocks;

        data.nlbaf = 0;
        data.flbas = 0;

        data.lbaf[0] = {.lbads = 9};

        vm->cpus[0].dma_write(cmd.prp0, {(uint8_t*)&data, sizeof(data)});
    } else if(cns == 1) {
        ControllerIdentify data{};
        data.vid = 0x8086;
        data.ss_vid = 0x8086;
        memcpy(data.serial, "000000000000000000", 20);
        memcpy(data.model, "Luna NVMe Controller", 22);
        memcpy(data.revision, "Luna NVMe 1.0", 15);
        memcpy(data.subnqn, "nqn.2014-08.org.nvmexpress:uuid:fbaa176f-c4fb-4c4b-9426-2c1e195fe524", 69);
        data.mdts = 0;
        data.nn = 1; // 1 Namespace

        vm->cpus[0].dma_write(cmd.prp0, {(uint8_t*)&data, sizeof(data)});
    } else if(cns == 2) {
        if(cmd.nsid == 0xFFFF'FFFF || cmd.nsid == 0xFFFF'FFFE)
            return false;

        ASSERT(cmd.nsid == 0);

        auto* nsid_list = new uint32_t[1024];
        memset(nsid_list, 0, 1024 * sizeof(uint32_t));
        nsid_list[0] = 1; // Only NS we support

        vm->cpus[0].dma_write(cmd.prp0, {(uint8_t*)nsid_list, 1024 * sizeof(uint32_t)});

        delete[] nsid_list;
    } else if(cns == 3) {
        UUID uuid{"c09cfac3-6cf6-41a0-8f33-56c71ccd91ff"};

        uint8_t buf[20];
        buf[0] = 3; // UUID
        buf[1] = uuid.span().size_bytes();
        memcpy(buf + 4, uuid.span().data(), uuid.span().size_bytes());

        vm->cpus[0].dma_write(cmd.prp0, {(uint8_t*)buf, 20 * sizeof(uint8_t)});
    } else if(cns == 0x6 && csi == 0x0) {
        NVMSpecificIdentifyCNS06hCSI00h data{};
        data.vsl = 0; // Do not support Verify command
        data.wzsl = 0; // Do not support WriteZeroes command
        data.wusl = 0; // Do not support WriteUncorrectable command
        data.dmrl = 0; // Do not support Dataset Management command
        data.dmrsl = 0; // ^
        data.dmsl = 0; // ^

        vm->cpus[0].dma_write(cmd.prp0, {(uint8_t*)&data, sizeof(data)});
    } else {
        print("nvme: Identify Unknown CNS {}\n", cns);
        return false;
    }

    return true;
}