#pragma once

#include <Luna/net/if.hpp>
#include <Luna/net/ipv4.hpp>

namespace net::udp {
    struct [[gnu::packed]] Header {
        uint16_t source_port;
        uint16_t dest_port;
        uint16_t len;
        uint16_t checksum;
    };

    void send(Interface& nic, Address addr, const std::span<uint8_t>& packet);
} // namespace net::udp
