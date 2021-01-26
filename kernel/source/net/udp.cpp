#include <Luna/net/udp.hpp>

void net::udp::send(Interface& nic, Address addr, const std::span<uint8_t>& packet) {
    auto len = sizeof(Header) + packet.size_bytes();
    auto* data = new uint8_t[len];

    auto& header = *(Header*)data;
    header.dest_port = bswap<uint16_t>(addr.port);
    header.source_port = bswap<uint16_t>(0); // TODO
    header.len = bswap<uint16_t>(len);
    
    memcpy(data + sizeof(Header), packet.data(), packet.size_bytes());

    if((nic.nic->checksum_offload & cs_offload::udp) == 0)
        PANIC("TODO: Do UDP Checksum");

    std::span<uint8_t> ip_packet{data, len};
    ipv4::send(nic, addr, ipv4::proto_udp, ip_packet);

    delete[] data;
}