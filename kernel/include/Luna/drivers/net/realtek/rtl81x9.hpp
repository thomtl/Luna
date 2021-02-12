#pragma once

#include <Luna/common.hpp>
#include <Luna/drivers/pci.hpp>
#include <Luna/mm/iovmm.hpp>

#include <Luna/net/if.hpp>

namespace rtl81x9 {
    struct [[gnu::packed]] Regs {
        uint8_t id[6];
        uint16_t reserved;
        uint8_t mar[8];
        uint64_t dtccr;
        uint64_t reserved_0;
        uint32_t tnpds_low;
        uint32_t tnpds_high;
        uint32_t thpds_low;
        uint32_t thpds_high;
        uint32_t flash;
        uint16_t erbcr;
        uint8_t ersr;
        uint8_t cr;
        uint8_t txpoll;
        uint8_t reserved_1[3];
        uint16_t imr;
        uint16_t isr;
        uint32_t tcr;
        uint32_t rcr;
        uint32_t tctr;
        uint32_t mpc;
        uint8_t cr9346;
        uint8_t config[6];
        uint8_t reserved_2;
        uint32_t timerint;
        uint16_t mulint;
        uint16_t reserved_3;
        uint32_t phyar;
        uint32_t tbicsr0;
        uint16_t tbi_anar;
        uint16_t tbi_lpar;
        uint8_t phystatus;
        uint8_t reserved_4[23];
        uint64_t wakeup0;
        uint64_t wakeup1;
        uint64_t wakeup2LD;
        uint64_t wakeup2HD;
        uint64_t wakeup3LD;
        uint64_t wakeup3HD;
        uint64_t wakeup4LD;
        uint64_t wakeup4HD;
        uint16_t crc[5];
        uint8_t reserved_5[0xC];
        uint16_t rms;
        uint32_t reserved_7;
        uint16_t cpcr;
        uint16_t reserved_8;
        uint32_t rdsar_low;
        uint32_t rdsar_high;
        uint8_t etthr;
        uint8_t reserved_9[3];
        uint32_t fer;
        uint32_t femr;
        uint32_t fpsr;
        uint32_t ffer;
    };

    namespace isr {
        constexpr uint16_t rx_ok = (1 << 0);
        constexpr uint16_t rx_err = (1 << 1);
        constexpr uint16_t tx_ok = (1 << 2);
        constexpr uint16_t tx_err = (1 << 3);
        constexpr uint16_t rx_unavailable = (1 << 4);
        constexpr uint16_t link_change = (1 << 5);
        constexpr uint16_t rx_fifo_overflow = (1 << 6);
        constexpr uint16_t tx_unavailable = (1 << 7);
        constexpr uint16_t sw_int = (1 << 8);
        constexpr uint16_t timeout = (1 << 14);
        constexpr uint16_t serr = (1 << 15);
    
    } // namespace isr

    namespace cr {
        constexpr uint8_t tx_enable = (1 << 2);
        constexpr uint8_t rx_enable = (1 << 3);
        constexpr uint8_t reset = (1 << 4);
    } // namespace cr
    
    namespace tcr {
        constexpr uint32_t mxdma_unlimited = (0b111 << 8);
        constexpr uint32_t no_crc = (1 << 16);
        constexpr uint32_t ifg_normal = (0b11 << 24);
    } // namespace tcr

    namespace rcr {
        constexpr uint32_t mxdma_unlimited = (0b111 << 8);
        constexpr uint32_t rxftr_none = (0b111 << 13);
    } // namespace rcr

    namespace txpoll {
        constexpr uint8_t poll_high_prio = (1 << 7);
        constexpr uint8_t poll_normal_prio = (1 << 6);
    } // namespace txpoll
    
    namespace cpcr {
        constexpr uint16_t rx_vlan = (1 << 6);
        constexpr uint16_t rx_chksum = (1 << 5);

        constexpr uint16_t pci_mul_rw = (1 << 3);

        constexpr uint16_t rx_enable = (1 << 1);
        constexpr uint16_t tx_enable = (1 << 0);
    } // namespace cpcr

    namespace cr9346 {
        constexpr uint8_t unlock_regs = 0xC0;
        constexpr uint8_t lock_regs = 0;
    } // namespace cr9346
    
    

    struct [[gnu::packed]] TxDescriptor {
        uint32_t flags;
        uint32_t vlan;
        uint32_t base_low;
        uint32_t base_high;
    };

    struct [[gnu::packed]] RxDescriptor {
        uint32_t flags;
        uint32_t vlan;
        uint32_t base_low;
        uint32_t base_high;
    };

    namespace tx_flags {
        constexpr uint32_t own = (1 << 31);
        constexpr uint32_t eor = (1 << 30);
        constexpr uint32_t fs = (1 << 29);
        constexpr uint32_t ls = (1 << 28);

        constexpr uint32_t ip_cs = (1 << 29);
        constexpr uint32_t udp_cs = (1 << 31);
    } // namespace tx_flags
    
    constexpr size_t n_descriptor_sets = 256;
    constexpr size_t mtu = 1536;

    struct [[gnu::packed]] SetBuffers {
        struct [[gnu::packed]] {
            uint8_t buf[mtu];
        } descriptor[n_descriptor_sets];
    };

    struct Nic : public net::Nic {
        Nic(pci::Device& device, uint16_t did);

        bool send_packet(const net::Mac& dst, uint16_t ethertype, const std::span<uint8_t>& packet, uint32_t offload);
        net::Mac get_mac() const { return mac; }

        private:
        void handle_irq();
        void handle_tx_ok();

        iovmm::Iovmm mm;
        volatile Regs* regs;

        volatile TxDescriptor* tx;
        volatile RxDescriptor* rx;

        volatile SetBuffers* tx_set;
        volatile SetBuffers* rx_set;

        iovmm::Iovmm::Allocation tx_alloc, rx_alloc, tx_set_alloc, rx_set_alloc;

        net::Mac mac;
        size_t tx_index;
    };
} // namespace rtl81x9
