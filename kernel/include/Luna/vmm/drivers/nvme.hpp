
#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

#include <Luna/misc/log.hpp>
#include <Luna/vmm/drivers/pci/pci_driver.hpp>

#include <Luna/fs/vfs.hpp>


namespace vm::nvme {
    constexpr size_t bar_size = 0x4000;


    constexpr size_t max_queue_entries = 256;

    // Max 64 Queue Entries, Queues have to be contiguous, 4 byte db stride, 
    // NVM Command Set supported, 4KiB min page size, 4KiB max page size
    constexpr uint64_t cap = (max_queue_entries - 1) | (1 << 16) | (1ull << 37);
                             

    namespace regs {
        constexpr size_t cap_low = 0;
        constexpr size_t cap_high = 4;

        constexpr size_t version = 8;

        constexpr size_t cc = 0x14;
        constexpr uint32_t cc_en = (1 << 0);

        constexpr size_t csts = 0x1C;
        constexpr uint32_t csts_rdy = (1 << 0);

        constexpr size_t aqa = 0x24;
        constexpr size_t asq = 0x28;
        constexpr size_t acq = 0x30;
    } // namespace regs


    struct [[gnu::packed]] SubmissionEntry {
        uint32_t opcode : 8;
        uint32_t fuse : 2;
        uint32_t reserved : 4;
        uint32_t prp : 2;
        uint32_t cid : 16;

        uint32_t nsid;
        uint64_t reserved_0;
        uint64_t metadata;
        uint64_t prp0;
        uint64_t prp1;

        uint32_t cmd_data[6];
    };
    static_assert(sizeof(SubmissionEntry) == 64);

    struct [[gnu::packed]] CompletionEntry {
        uint32_t cmd_specific;
        uint32_t reserved;
        uint16_t sq_head;
        uint16_t sq_id;
        uint16_t cid;
        uint16_t phase : 1;
        uint16_t status : 15;
    };
    static_assert(sizeof(CompletionEntry) == 16);   

    struct [[gnu::packed]] ControllerIdentify {
        uint16_t vid;
        uint16_t ss_vid;
        char serial[20];
        char model[40];
        char revision[8];

        uint8_t rab;
        uint8_t ieee[3];
        uint8_t cmic;
        uint8_t mdts;

        uint8_t ignored[516 - 78];

        uint32_t nn;
    };

    struct NamespaceIdentify {
        uint64_t nsze;
        uint64_t ncap;
        uint64_t nuse;
        uint8_t  nsfeat;
        uint8_t  nlbaf;
        uint8_t  flbas;

        uint8_t ignored[128 - 27];

        struct {
            uint16_t ms = 0;
            uint8_t lbads = 0;
            uint8_t rp = 0;
            uint8_t res = 0;
        } lbaf[16];
    };

    struct Driver : vm::pci::PCIDriver, public vm::AbstractMMIODriver {
        Driver(Vm* vm, pci::HostBridge* bridge, uint8_t slot, uint8_t func, vfs::File* file): PCIDriver{vm}, vm{vm}, file{file} {
            bridge->register_pci_driver(pci::DeviceID{0, 0, slot, func}, this);

            pci_space.header.vendor_id = 0x8086;
            pci_space.header.device_id = 0xF1A5; // Intel SSD 600P Series
            pci_space.header.revision = 3;

            pci_space.header.subsystem_vendor_id = 0x8086;
            pci_space.header.subsystem_device_id = 0x390A;

            pci_space.header.class_id = 1; // Storage
            pci_space.header.subclass = 8; // NVMe
            pci_space.header.prog_if = 2; // NVMe IO Controller

            if(func > 0)
                pci_space.header.header_type = 0x80;

            pci_init_bar(0, bar_size, true, true); // MMIO, 64bit
        }

        void register_mmio_driver([[maybe_unused]] Vm* vm) { }

        void mmio_write(uintptr_t addr, uint64_t value, uint8_t size) {
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
                }
                
                if(!(cc & regs::cc_en) && (value & regs::cc_en)) // Off to On
                    csts |= regs::csts_rdy;

                ASSERT(((value >> 4) & 7) == 0); // NVM Command Set not selected
                ASSERT(((value >> 7) & 0xF) == 0); // Pagesize != 4KiB
                ASSERT(((value >> 11) & 7) == 0); // Not Round Robin

                sq_entry_size = 1 << ((value >> 16) & 0xF);
                cq_entry_size = 1 << ((value >> 20) & 0xF);

                cc = value;
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
                else
                    queues[qid].cq_head = value;
            } else {
                print("nvme: Unknown MMIO write {:#x} <- {:#x} ({})\n", reg, value, size);
                PANIC("Unknown reg");
            }
        }

        uint64_t mmio_read(uintptr_t addr, uint8_t size) {
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
            else {
                print("nvme: Unknown MMIO read from {:#x}, size: {}\n", reg, size);
                PANIC("Unknown reg");
            }
            
            return 0;
        }

        void pci_handle_write(uint16_t reg, uint32_t value, [[maybe_unused]] uint8_t size) {
            print("nvme: Unhandled PCI write, reg: {:#x}, value: {:#x}\n", reg, value);
        }

        uint32_t pci_handle_read(uint16_t reg, uint8_t size) {
            print("nvme: Unhandled PCI read, reg: {:#x}, size: {:#x}\n", reg, (uint16_t)size);

            return 0;
        }

        void pci_update_bars() {
            if(!(pci_space.header.command & (1 << 1)))
                return;

            uint64_t base = (pci_space.header.bar[0] & ~0xF) | ((uint64_t)pci_space.header.bar[1] << 32);
            
            if(mmio_enabled)
                vm->mmio_map[mmio_base] = {nullptr, 0};

            vm->mmio_map[base] = {this, bar_size};
            mmio_base = base;
            mmio_enabled = true;
        }

        private:
        void kick_queue(uint16_t qid, uint32_t value) {
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

        struct Queue;

        CompletionEntry admin_queue_handle(const SubmissionEntry& cmd) {
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
                //ASSERT(!(cmd.cmd_data[1] & (1 << 1))); // No IRQs

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

                    queues[qid] = queue;

                    c.status = 0;
                }
            } else if(opcode == 6) { // Identify
                c.status = handle_admin_identify(cmd) ? 0 : 0xB;
            } else {
                print("nvme: Unknown Admin Opcode: {}\n", opcode);
                PANIC("Unknown op");
            }

            return c;
        }

        CompletionEntry nvm_queue_handle(const SubmissionEntry& cmd) {
            CompletionEntry c{};

            auto opcode = cmd.opcode;
            if(opcode == 2) { // Read
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

        void cq_push(uint16_t qid, CompletionEntry entry) {
            auto& queue = queues[qid];
            entry.phase = queue.phase;

            vm->cpus[0].dma_write(queue.cq_base + (queue.cq_tail * cq_entry_size), {(uint8_t*)&entry, cq_entry_size});
            queue.cq_tail = (queue.cq_tail + 1) % queue.cqs;

            if(queue.cq_tail == 0) // Just wrapped around
                queue.phase = !queue.phase;
        }

        bool handle_admin_identify(const SubmissionEntry& cmd) {
            auto cns = cmd.cmd_data[0] & 0xFF;

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
                data.mdts = 0;
                data.nn = 1; // 1 Namespace

                vm->cpus[0].dma_write(cmd.prp0, {(uint8_t*)&data, sizeof(data)});
            } else {
                print("nvme: Identify Unknown CNS {}\n", cns);
                PANIC("Unknown CNS");
            }

            return true;
        }

        bool mmio_enabled = false;
        uintptr_t mmio_base;

        uint32_t cc, csts;

        struct Queue {
            uintptr_t cq_base, sq_base;
            size_t cqs, sqs;

            uint16_t cq_head, cq_tail;
            uint16_t sq_head, sq_tail;

            bool phase = true;
        };
        std::unordered_map<uint16_t, Queue> queues;

        uint8_t cq_entry_size, sq_entry_size;

        vm::Vm* vm;
        vfs::File* file;
    };
} // namespace vm::nvme