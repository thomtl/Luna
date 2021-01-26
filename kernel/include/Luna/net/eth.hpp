#pragma once

#include <Luna/common.hpp>

namespace net::eth {
    struct [[gnu::packed]] Header {
        uint8_t dst_mac[6];
        uint8_t src_mac[6];
        uint16_t ethertype;
    };

    constexpr uint16_t type_ipv4 = 0x800;
    constexpr uint16_t type_arp = 0x806;
} // namespace net::eth
