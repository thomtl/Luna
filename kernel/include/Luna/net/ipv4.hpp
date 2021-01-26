#pragma once

#include <Luna/common.hpp>
#include <Luna/net/if.hpp>

namespace net::ipv4 {
    struct [[gnu::packed]] Header {
        uint8_t type;
        uint8_t tos;
        uint16_t len;
        uint16_t id;
        uint16_t frag;
        uint8_t ttl;
        uint8_t proto;
        uint16_t checksum;
        uint32_t source_ip;
        uint32_t dest_ip;
    };

    constexpr uint8_t version = 4;

    constexpr uint8_t proto_udp = 0x11;

    void send(Interface& nic, Address addr, uint8_t proto, const std::span<uint8_t>& packet);
} // namespace net::ipv4
