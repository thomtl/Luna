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

    struct ATACommand {
        uint8_t command;
        uint8_t features;
        uint64_t lba;
        uint16_t n_sectors;

        bool write;
    };

    struct ATAPICommand {
        uint16_t packet[16];
    };

    struct DriverDevice {
        bool atapi;
        void* userptr;
        void (*ata_cmd)(void* userptr, const ATACommand& cmd, std::span<uint8_t>& xfer);
        void (*atapi_cmd)(void* userptr, const ATAPICommand& cmd);
    };

    struct Device {
        DriverDevice driver;

        uint8_t identify[512];
        bool lba48;
        size_t n_sectors, sector_size;
    };
    
    void register_device(DriverDevice& dev);
} // namespace ata
