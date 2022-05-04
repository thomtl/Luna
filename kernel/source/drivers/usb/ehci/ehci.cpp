#include <Luna/drivers/usb/ehci/ehci.hpp>

#include <Luna/drivers/pci.hpp>
#include <Luna/drivers/timers/timers.hpp>
#include <Luna/misc/log.hpp>


static void handoff_bios(pci::Device& dev) {    
    volatile auto* cap = (volatile ehci::HostCapabilityRegs*)(dev.read_bar(0).base + phys_mem_map);

    uint8_t eecp = (cap->hcc_params >> 8) & 0xFF;
    if(eecp == 0) {
        print("ehci: No OS <=> BIOS Handoff support\n");
        return;
    }

    uint16_t usblegsup = eecp + ehci::usblegsup::eecp_off;

    print("ehci: Doing OS <=> BIOS Handoff ... ");
    dev.write<uint32_t>(usblegsup, dev.read<uint32_t>(usblegsup) | ehci::usblegsup::os_owned_semaphore);
    while(dev.read<uint32_t>(usblegsup) & ehci::usblegsup::bios_owned_semaphore)
        asm("nop");
    print("Done\n");
}

static void init(pci::Device&) {
    print("ehci: TODO: Init device\n");
}

static pci::Driver driver = {
    .name = "eHCI Driver",
    .bios_handoff = handoff_bios,
    .init = init,

    .match = pci::match::class_code | pci::match::subclass_code | pci::match::protocol_code,
    .class_code = 0xC,
    .subclass_code = 0x3,
    .protocol_code = 0x20
};
DECLARE_PCI_DRIVER(driver);