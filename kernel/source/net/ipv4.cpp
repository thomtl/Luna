#include <Luna/net/ipv4.hpp>
#include <Luna/net/eth.hpp>
#include <Luna/misc/misc.hpp>

void net::ipv4::send(net::Interface& nic, net::Address addr, uint8_t proto, const std::span<uint8_t>& packet) {
    auto len = sizeof(Header) + packet.size_bytes();
    auto* data = new uint8_t[len];

    auto& header = *(Header*)data;
    
    header.type = 0x45;
    header.tos = 0;
    header.ttl = 64;
    header.frag = bswap<uint16_t>(1 << 14);
    header.proto = proto;

    static int id = 0;
    header.id = bswap<uint16_t>(id++);

    header.source_ip = nic.ip.data;
    header.dest_ip = addr.ip.data;

    header.len = bswap<uint16_t>(len);

    memcpy(data + sizeof(Header), packet.data(), packet.size_bytes());

    if(addr.mac.is_null()) {
        PANIC("TODO: ARP lookup");
    }


    if((nic.nic->checksum_offload & cs_offload::ipv4) == 0)
        PANIC("TODO: Do IPv4 Checksum");

    uint16_t offload = cs_offload::ipv4;
    if(proto == proto_udp)
        offload |= cs_offload::udp;

    std::span<uint8_t> eth_packet{data, len};
    nic.nic->send_packet(addr.mac, eth::type_ipv4, eth_packet, offload);

    delete[] data;
}