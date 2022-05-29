#pragma once

#include <Luna/common.hpp>
#include <std/span.hpp>

namespace scsi {
    namespace commands {
        namespace inquiry {
            constexpr uint8_t command = 0x12;

            struct [[gnu::packed]] Packet {
                uint8_t command;
                struct {
                    uint8_t evpd : 1;
                    uint8_t obsolete : 1;
                    uint8_t reserved : 6;
                } flags;
                uint8_t page_code;
                uint16_t allocation_length;
                uint8_t control;
            };
        } // namespace inquiry

        namespace read_capacity
        {
            constexpr uint8_t command = 0x25;

            struct [[gnu::packed]] Packet {
                uint8_t command;
                uint8_t reserved;
                uint32_t lba;
                uint16_t reserved_0;
                uint8_t reserved_1;
                uint8_t control;
                uint16_t zero;
            };

            struct [[gnu::packed]] Response {
                uint32_t lba;
                uint32_t block_size;
            };
        } // namespace read_capacity

        namespace read12 {
            constexpr uint8_t command = 0xA8;

            struct [[gnu::packed]] Packet {
                uint8_t command;
                struct {
                    uint8_t obsolete : 2;
                    uint8_t rarc : 1;
                    uint8_t fua : 1;
                    uint8_t dpo : 1;
                    uint8_t rdprotect : 3;
                };
                uint32_t lba;
                uint32_t length;
                struct {
                    uint8_t group_number : 5;
                    uint8_t reserved : 2;
                    uint8_t restricted : 1;
                };
                uint8_t control;
            };
            static_assert(sizeof(Packet) == 12);
        } // namespace read12

        namespace read10 {
            constexpr uint8_t command = 0x28;

            struct [[gnu::packed]] Packet {
                uint8_t command;
                struct {
                    uint8_t obsolete : 2;
                    uint8_t rarc : 1;
                    uint8_t fua : 1;
                    uint8_t dpo : 1;
                    uint8_t rdprotect : 3;
                };
                uint32_t lba;
                uint8_t group_number;
                uint16_t length;
                uint8_t control;
            };
            static_assert(sizeof(Packet) == 10);
        } // namespace read10

        namespace write10 {
            constexpr uint8_t command = 0x2A;

            struct [[gnu::packed]] Packet {
                uint8_t command;
                struct {
                    uint8_t obsolete : 2;
                    uint8_t reserved : 1;
                    uint8_t fua : 1;
                    uint8_t dpo : 1;
                    uint8_t rdprotect : 3;
                };
                uint32_t lba;
                uint8_t group_number;
                uint16_t length;
                uint8_t control;
            };
            static_assert(sizeof(Packet) == 10);
        } // namespace write10

        namespace write12 {
            constexpr uint8_t command = 0xAA;

            struct [[gnu::packed]] Packet {
                uint8_t command;
                struct {
                    uint8_t obsolete : 2;
                    uint8_t rarc : 1;
                    uint8_t fua : 1;
                    uint8_t dpo : 1;
                    uint8_t rdprotect : 3;
                };
                uint32_t lba;
                uint32_t length;
                struct {
                    uint8_t group_number : 5;
                    uint8_t reserved : 2;
                    uint8_t restricted : 1;
                };
                uint8_t control;
            };
            static_assert(sizeof(Packet) == 12);
        } // namespace write12
    } // namespace commands
    
    struct SCSICommand {
        uint8_t packet[32];
        uint8_t packet_len;
        bool write;
    };

    struct DriverDevice {
        void* userptr;
        bool (*scsi_cmd)(void* userptr, const SCSICommand& cmd, std::span<uint8_t>& xfer);

        uint8_t max_packet_size;
    };

    struct Device {
        DriverDevice driver;

        uint8_t type, version;
        size_t n_sectors, sector_size;
        bool inserted;
    };
    
    void register_device(DriverDevice& dev);
} // namespace scsi
