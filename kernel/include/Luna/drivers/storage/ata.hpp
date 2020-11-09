#pragma once

#include <Luna/common.hpp>
#include <std/span.hpp>

namespace ata {
    namespace commands {
        enum : uint8_t {
            Read = 0x20,
            ReadDMA = 0xC8,
            ReadExtended = 0x24,
            ReadExtendedDMA = 0x25,

            Write = 0x30,
            WriteDMA = 0xCA,
            WriteExtended = 0x34,
            WriteExtendedDMA = 0x35,

            Identify = 0xEC,
            IdentifyPI = 0xA1,

            SendPacket = 0xA0
        };
    } // namespace commands

    namespace packet_commands
    {
        namespace read_capacity
        {
            constexpr uint8_t command = 0x25;

            struct [[gnu::packed]] packet {
                uint8_t command;
                uint8_t reserved;
                uint32_t lba;
                uint16_t reserved_0;
                uint8_t reserved_1;
                uint8_t control;
                uint16_t zero;
            };

            struct [[gnu::packed]] response {
                uint32_t lba;
                uint32_t block_size;
            };
        } // namespace read_capacity
    } // namespace packet_commands
    

    struct ATACommand {
        uint8_t command;
        uint16_t features;
        uint64_t lba;
        uint16_t n_sectors;

        bool write, lba28;
    };

    struct ATAPICommand {
        uint16_t packet[16];

        bool write;
    };

    struct DriverDevice {
        bool atapi;
        void* userptr;
        void (*ata_cmd)(void* userptr, const ATACommand& cmd, std::span<uint8_t>& xfer);
        void (*atapi_cmd)(void* userptr, const ATAPICommand& cmd, std::span<uint8_t>& xfer);
    };

    struct Device {
        DriverDevice driver;

        uint8_t identify[512];
        bool lba48, inserted;
        size_t n_sectors, sector_size;
    };
    
    void register_device(DriverDevice& dev);
} // namespace ata
