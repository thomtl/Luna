#pragma once

#include <Luna/common.hpp>
#include <std/span.hpp>
#include <Luna/misc/format.hpp>

namespace net {
    namespace cs_offload {
        constexpr uint32_t ipv4 = (1 << 0);
        constexpr uint32_t udp = (1 << 1);
    } // namespace cs_offload
    

    struct Mac {
        uint8_t data[6];

        bool operator==(const Mac& other) const {
            return (memcmp(data, other.data, 6) == 0);
        }

        bool is_null() const {
            constexpr Mac null_mac = {0, 0, 0, 0, 0, 0};

            return (*this == null_mac);
        }
    };

    struct Ip {
        constexpr Ip(): data{0} {}
        constexpr Ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d): data{(uint32_t)a | ((uint32_t)b << 8) | ((uint32_t)c << 16) | ((uint32_t)d << 24)} {}

        uint32_t data;
    };

    struct Address {
        Mac mac;
        Ip ip;
        uint16_t port;
    };

    struct Nic {
        virtual bool send_packet(const Mac& dst, uint16_t ethertype, const std::span<uint8_t>& packet, uint32_t offload) = 0;
        virtual Mac get_mac() const = 0;

        uint32_t checksum_offload;
    };

    struct Interface {
        Nic* nic;
        Ip ip;
    };

    void register_nic(Nic* nic);
    Interface* get_default_if();
} // namespace net

namespace format {
    template<>
	struct formatter<net::Mac> {
		template<typename OutputIt>
		static void format(format_output_it<OutputIt>& it, [[maybe_unused]] format_args args, net::Mac item){
            format::format_to(it, "{:X}:{:X}:{:X}:{:X}:{:X}:{:X}", (uint16_t)item.data[0], (uint16_t)item.data[1], (uint16_t)item.data[2], (uint16_t)item.data[3], (uint16_t)item.data[4], (uint16_t)item.data[5]);
		}
	};

    template<>
	struct formatter<net::Ip> {
		template<typename OutputIt>
		static void format(format_output_it<OutputIt>& it, [[maybe_unused]] format_args args, net::Ip item){
            auto a = item.data & 0xFF;
            auto b = (item.data >> 8) & 0xFF;
            auto c = (item.data >> 16) & 0xFF;
            auto d = (item.data >> 24) & 0xFF;
            format::format_to(it, "{}.{}.{}.{}", a, b, c, d);
		}
	};
} // namespace format