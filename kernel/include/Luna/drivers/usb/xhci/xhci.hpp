#pragma once

#include <Luna/common.hpp>
#include <Luna/drivers/pci.hpp>

#include <Luna/mm/iovmm.hpp>

#include <std/vector.hpp>
#include <std/unordered_map.hpp>
#include <std/utility.hpp>

#include <Luna/drivers/usb/xhci/context.hpp>
#include <Luna/drivers/usb/xhci/trb.hpp>

#include <Luna/drivers/usb/usb.hpp>

namespace xhci {
    struct [[gnu::packed]] CapabilityRegs {
        uint8_t caplength;
        uint8_t reserved;
        uint16_t hci_version;
        uint32_t hcsparams1;
        uint32_t hcsparams2;
        uint32_t hcsparams3;
        uint32_t hccparams1;
        uint32_t dboff;
        uint32_t rtsoff;
        uint32_t hccparams2;
    };

    struct [[gnu::packed]] OperationalRegs {
        uint32_t usbcmd;
        uint32_t usbsts;
        uint32_t pagesize;
        uint64_t reserved;
        uint32_t dnctrl;
        uint64_t crcr;
        uint8_t reserved_0[16];
        uint64_t dcbaap;
        uint32_t config;
        uint8_t reserved_1[0x3C4];
        struct [[gnu::packed]] {
            uint32_t portsc;
            uint32_t portpmsc;
            uint32_t portli;
            uint32_t porthlpmc;
        } ports[];
    };

    namespace usbcmd {
        constexpr uint32_t run = (1 << 0);
        constexpr uint32_t reset = (1 << 1);
        constexpr uint32_t irq_enable = (1 << 2);
        constexpr uint32_t host_system_err_enable = (1 << 3);
    } // namespace usbcmd

    namespace usbsts {
        constexpr uint32_t halted = (1 << 0);
        constexpr uint32_t host_system_error = (1 << 2);
        constexpr uint32_t irq = (1 << 3);
        constexpr uint32_t not_ready = (1 << 11);
    } // namespace usbsts

    namespace portsc {
        constexpr uint32_t connect_status = (1 << 0);
        constexpr uint32_t enabled = (1 << 1);
        constexpr uint32_t overcurrent_active = (1 << 3);
        constexpr uint32_t reset = (1 << 4);
        constexpr uint32_t port_power = (1 << 9);
        constexpr uint32_t write_strobe = (1 << 16);
        constexpr uint32_t connect_status_change = (1 << 17);
        constexpr uint32_t port_enabled_change = (1 << 18);
        constexpr uint32_t warm_port_reset_change = (1 << 19);
        constexpr uint32_t overcurrent_change = (1 << 20);
        constexpr uint32_t reset_change = (1 << 21);
        constexpr uint32_t link_status_change = (1 << 22);
        constexpr uint32_t config_error_change = (1 << 23);
        constexpr uint32_t cold_attach_status = (1 << 24);
        constexpr uint32_t wake_on_connect = (1 << 25);
        constexpr uint32_t wake_on_disconnect = (1 << 26);
        constexpr uint32_t wake_on_overcurrent = (1 << 27);
        constexpr uint32_t removeable = (1 << 30);
        constexpr uint32_t warm_reset = (1 << 31);

        constexpr uint32_t status_change_bits = connect_status_change | port_enabled_change | overcurrent_change | reset_change | link_status_change;

        // Values for Port Speed bits 10:13
        constexpr uint32_t full_speed = 1;
        constexpr uint32_t low_speed = 2;
        constexpr uint32_t high_speed = 3;
        constexpr uint32_t super_speed = 4;

    } // namespace portsc
    

    struct [[gnu::packed]] RuntimeRegs {
        uint32_t microframe_index;
        uint8_t reserved[0x1C];

        struct [[gnu::packed]] {
            uint32_t iman;
            uint32_t imod;
            uint32_t erst_size;
            uint32_t reserved;
            uint64_t erst_base;
            uint64_t erst_dequeue;
        } interrupters[];
    };

    namespace iman {
        constexpr uint32_t irq_pending = (1 << 0);
        constexpr uint32_t irq_enable = (1 << 1);
    } // namespace iman
    

    struct [[gnu::packed]] ERSTEntry {
        uint64_t ring_base;
        uint16_t ring_size;
        uint16_t reserved;
        uint32_t reserved_0;
    };

    struct [[gnu::packed]] LegacyCap {
        uint32_t usblegsup;
        uint32_t usblegctlsts;
    };

    namespace usblegsup {
        constexpr uint32_t bios_owned = (1 << 16);
        constexpr uint32_t os_owned = (1 << 24);
    } // namespace usblegsup
    

    struct [[gnu::packed]] ProtocolCap {
        uint32_t version;
        uint32_t name;
        uint32_t ports_info;
        uint32_t slot_type;
        uint32_t speeds[];
    };

    struct Protocol {
        uint8_t major, minor, slot_type;
        uint32_t port_off, port_count;
    };

    struct HCI {
        HCI(pci::Device& dev);

        private:
        struct Port {
            HCI* hci;

            Protocol* proto;
            bool has_pair, active;

            size_t port_id, slot_id;
            size_t other_port, offset;
            size_t max_packet_size;

            uint8_t speed;
            
            std::lazy_initializer<DeviceContext> dev_ctx;
            std::lazy_initializer<InputContext> in_ctx;

            std::lazy_initializer<TransferRing> ep0_queue;
            
            std::unordered_map<uint8_t, std::pair<usb::EndpointData, TransferRing*>> ep_rings;
        };

        bool setup_ep(Port& port, const usb::EndpointData& ep);
        bool send_ep0_control(Port& port, const usb::DeviceRequestPacket& packet, bool write, size_t len, uint8_t* buf);
        bool send_ep_bulk(Port& port, uint8_t epid, std::span<uint8_t> data);

        void enumerate_ports();

        bool reset_port(Port& port);
        void intel_enable_ports();

        void handle_irq();
        void reset_controller();

        volatile CapabilityRegs* cap;
        volatile OperationalRegs* op;
        volatile RuntimeRegs* run;
        volatile uint32_t* db, *ext_caps;

        volatile ERSTEntry* erst;

        pci::Device* device;
        iovmm::Iovmm mm;

        iovmm::Iovmm::Allocation dcbaap_alloc;
        volatile uint64_t* dcbaap;

        size_t n_slots, n_interrupters, n_ports, context_size, erst_max, n_scratchbufs, page_size;
        uint32_t quirks;
        enum {
            quirkIntel = (1 << 0)
        };
        bool port_power_control;

        std::vector<Protocol> protocols;

        std::lazy_initializer<TRBRing> evt_ring;
        std::lazy_initializer<CmdRing> cmd_ring;

        std::vector<Port> ports;
        std::unordered_map<uint8_t, Port*> port_by_slot;
    };
} // namespace xhci
