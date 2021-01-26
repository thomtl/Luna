#include <Luna/net/if.hpp>

#include <Luna/misc/log.hpp>
#include <std/linked_list.hpp>

std::linked_list<net::Interface> interfaces;

void net::register_nic(Nic* nic) {
    auto& interface = interfaces.emplace_back();
    interface.nic = nic;
    interface.ip = {192, 168, 2, 70}; // TODO: DHCP

    print("net: Registered Interface with IP: {}\n", interface.ip);
}

net::Interface* net::get_default_if() {
    ASSERT(interfaces.size() >= 1);

    return &interfaces[0]; // TODO
}