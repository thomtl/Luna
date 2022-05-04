#pragma once

#include <Luna/common.hpp>

namespace ehci {
    struct [[gnu::packed]] HostCapabilityRegs {
        uint8_t cap_length;
        uint8_t reserved;
        uint16_t hci_version;
        uint32_t hcs_params;
        uint32_t hcc_params;
        uint64_t hcsp_portroute;
    };

    namespace usblegsup {
        constexpr uint16_t eecp_off = 0;

        constexpr uint32_t bios_owned_semaphore = (1 << 16);
        constexpr uint32_t os_owned_semaphore = (1 << 24);
    } // namespace usblegsup
} // namespace ehci
