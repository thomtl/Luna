#pragma once

#include <Luna/common.hpp>
#include <Luna/drivers/pci.hpp>

#include <Luna/drivers/storage/ata.hpp>


namespace ahci
{
    struct [[gnu::packed]] Ghcr {
        uint32_t cap;
        uint32_t ghc;
        uint32_t is;
        uint32_t pi;
        uint32_t vs;
        uint32_t ccc_ctl;
        uint32_t ccc_ports;
        uint32_t em_loc;
        uint32_t em_ctl;
        uint32_t cap2;
        uint32_t bohc;
    };
    static_assert(sizeof(Ghcr) == 44);

    struct [[gnu::packed]] Prs {
        uint32_t clb;
        uint32_t clbu;
        uint32_t fb;
        uint32_t fbu;
        uint32_t is;
        uint32_t ie;
        uint32_t cmd;
        uint32_t reserved;
        uint32_t tfd;
        uint32_t sig;
        uint32_t ssts;
        uint32_t sctl;
        uint32_t serr;
        uint32_t sact;
        uint32_t ci;
        uint32_t sntf;
        uint32_t fbs;
        uint32_t devslp;
        uint8_t reserved_0[40];
        uint8_t vendor_specific[16];
    };
    static_assert(sizeof(Prs) == 128);

    struct [[gnu::packed]] Hba {
        Ghcr ghcr;
        uint8_t reserved[52];
        uint8_t nvmhci[64];
        uint8_t vendor_specific[96];
        Prs ports[];
    };

    struct [[gnu::packed]] CmdHeader {
        struct {
            uint32_t cfl : 5;
            uint32_t atapi : 1;
            uint32_t write : 1;
            uint32_t prefetchable : 1;
            uint32_t srst_control : 1;
            uint32_t bist : 1;
            uint32_t clear : 1;
            uint32_t reserved : 1;
            uint32_t pmp : 4;
            uint32_t prdtl : 16;
        } flags;
        static_assert(sizeof(flags) == 4);

        uint32_t prdbc;
        uint32_t ctba;
        uint32_t ctbau;

        uint8_t reserved[16];
    };
    static_assert(sizeof(CmdHeader) == 32);

    enum {
        FISTypeH2DRegister = 0x27
    };

    struct [[gnu::packed]] H2DRegisterFIS {
        uint8_t type;
        struct {
            uint8_t pm_port : 4;
            uint8_t reserved : 3;
            uint8_t c : 1;
        } flags;
        uint8_t command;
        uint8_t features;
        uint8_t lba_0;
        uint8_t lba_1;
        uint8_t lba_2;
        uint8_t dev_head;
        uint8_t lba_3;
        uint8_t lba_4;
        uint8_t lba_5;
        uint8_t features_exp;
        uint8_t sector_count_low;
        uint8_t sector_count_high;
        uint8_t reserved_0;
        uint8_t control;
        uint8_t reserved_1[4];
    };
    static_assert(sizeof(H2DRegisterFIS) == 20);

    struct [[gnu::packed]] Prdt {
        uint32_t low;
        uint32_t high;
        uint32_t reserved;
        struct {
            uint32_t byte_count : 22;
            uint32_t reserved : 9;
            uint32_t irq_on_completion : 1;
        } flags;

        static uint32_t calculate_bytecount(size_t count){
            ASSERT((count % 2) == 0);

            ASSERT(count <= 0x3FFFFF);

            return ((((count + 1) & ~1) - 1) & 0x3FFFFF);
        }            
    };
    static_assert(sizeof(Prdt) == 16);

    struct [[gnu::packed]] CmdTable {
        uint8_t fis[64];
        uint8_t packet[16];
        uint8_t reserved[48];
        Prdt prdts[];
    };

    constexpr uint32_t make_version(uint16_t major, uint8_t minor, uint8_t patch) { return (major << 16) | (minor << 8) | patch; }

    class Controller {
        public:
        Controller(pci::Device* device);

        struct Port {
            uint8_t port;
            volatile Prs* regs;
            Controller* controller;

            union PhysRegion {
                struct {
                    CmdHeader command_headers[32];
                    uint8_t receive_fis[256];
                };
                uint8_t padding[0x1000];
            };

            PhysRegion* region;

            void wait_idle();
            void wait_ready();

            uint8_t get_free_cmd_slot();
            std::pair<uint8_t, CmdTable*> allocate_command(size_t n_prdts);

            void send_ata_cmd(const ata::ATACommand& cmd, uint8_t* data, size_t transfer_len);
            void send_atapi_cmd(const ata::ATAPICommand& cmd, uint8_t* data, size_t transfer_len);
        };

        private:
        bool bios_handshake();

        volatile Hba* regs;
        uint8_t n_allocated_ports, n_command_slots;
        bool a64;
        pci::Device* device;

        friend struct Port;

        std::vector<Port> ports;
    };

    void init();
} // namespace ahci
