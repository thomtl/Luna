
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
        constexpr size_t cap_low = 0x0;
        constexpr size_t cap_high = 0x4;

        constexpr size_t version = 0x8;
        constexpr size_t intms = 0xC;
        constexpr size_t intmc = 0x10;

        constexpr size_t cc = 0x14;
        constexpr uint32_t cc_en = (1 << 0);

        constexpr size_t csts = 0x1C;
        constexpr uint32_t csts_rdy = (1 << 0);

        constexpr size_t aqa = 0x24;
        constexpr size_t asq = 0x28;
        constexpr size_t acq = 0x30;

        constexpr size_t cmbsz = 0x3C;
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

        uint16_t oncs;
        uint16_t fuses;
        uint8_t fna;
        uint8_t vwc;
        uint16_t awun;
        uint16_t awupf;
        uint8_t nvscc;
        uint8_t nwpc;
        uint16_t acwu;
        uint16_t reserved;
        uint32_t sgls;
        uint32_t mnan;
        uint8_t reserved_0[767 - 544 + 1];
        uint8_t subnqn[1023 - 768 + 1];
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

    struct [[gnu::packed]] NVMSpecificIdentifyCNS06hCSI00h {
        uint8_t vsl;
        uint8_t wzsl;
        uint8_t wusl;
        uint8_t dmrl;
        uint32_t dmrsl;
        uint64_t dmsl;
        uint8_t reserved[4096 - 16];
    };
    static_assert(sizeof(NVMSpecificIdentifyCNS06hCSI00h) == 4096);

    struct Driver : vm::pci::PCIDriver, public vm::AbstractMMIODriver {
        Driver(Vm* vm, pci::HostBridge* bridge, uint8_t slot, uint8_t func, vfs::File* file);

        void register_mmio_driver([[maybe_unused]] Vm* vm) { }

        void mmio_write(uintptr_t addr, uint64_t value, uint8_t size);
        uint64_t mmio_read(uintptr_t addr, uint8_t size);

        void pci_handle_write(uint16_t reg, uint32_t value, [[maybe_unused]] uint8_t size) {
            print("nvme: Unhandled PCI write, reg: {:#x}, value: {:#x}\n", reg, value);
        }

        uint32_t pci_handle_read(uint16_t reg, uint8_t size) {
            print("nvme: Unhandled PCI read, reg: {:#x}, size: {:#x}\n", reg, (uint16_t)size);

            return 0;
        }

        void pci_update_bars() {
            if(!(pci_space->header.command & (1 << 1)))
                return;

            uint64_t base = (pci_space->header.bar[0] & ~0xF) | ((uint64_t)pci_space->header.bar[1] << 32);
            
            if(mmio_enabled)
                vm->mmio_map[mmio_base] = {nullptr, 0};

            vm->mmio_map[base] = {this, bar_size};
            mmio_base = base;
            mmio_enabled = true;
        }

        private:
        void kick_queue(uint16_t qid, uint32_t value);

        struct Queue;

        CompletionEntry admin_queue_handle(const SubmissionEntry& cmd);

        CompletionEntry nvm_queue_handle(const SubmissionEntry& cmd);

        void cq_push(uint16_t qid, CompletionEntry entry);

        bool handle_admin_identify(const SubmissionEntry& cmd);

        void check_irq() {
            uint32_t v = irq_status & ~irq_mask;

            if(v)
                pci_set_irq_line(true);
            else
                pci_set_irq_line(false);
        }   

        void update_irqs(uint16_t qid, bool status) { // TODO: MSI-X/MSI
            ASSERT(qid < 32);

            if(status)
                irq_status |= (1 << qid);
            else
                irq_status &= ~(1 << qid);
            
            check_irq();
        }

        bool mmio_enabled = false;
        uintptr_t mmio_base;

        uint32_t cc, csts, irq_mask = 0, irq_status = 0;

        struct Queue {
            uintptr_t cq_base, sq_base;
            size_t cqs, sqs;

            uint16_t cq_head, cq_tail;
            uint16_t sq_head, sq_tail;

            bool phase = true, send_irqs = false;
        };
        std::unordered_map<uint16_t, Queue> queues;

        uint8_t cq_entry_size, sq_entry_size;

        vm::Vm* vm;
        vfs::File* file;
    };
} // namespace vm::nvme