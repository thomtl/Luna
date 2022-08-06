#include <Luna/drivers/pci.hpp>
#include <Luna/drivers/acpi.hpp>
#include <Luna/mm/vmm.hpp>

#include <Luna/cpu/cpu.hpp>
#include <Luna/cpu/tsc.hpp>

#include <Luna/misc/log.hpp>

#include <lai/core.h>
#include <acpispec/hw.h>

static std::vector<pci::Device> devices; 

static uintptr_t get_mcfg_device_addr(const acpi::Mcfg::Allocation& allocation, uint8_t bus, uint8_t slot, uint8_t func) {
    auto pa = allocation.base + (((bus - allocation.start_bus) << 20) | (slot << 15) | (func << 12));
    auto va = pa + phys_mem_map;
    vmm::get_kernel_context().map(pa, va, paging::mapPagePresent | paging::mapPageWrite);

    return va;
}

static acpi::Mcfg::Allocation get_mcfg_allocation(acpi::Mcfg* mcfg, uint16_t seg, uint8_t bus) {
    size_t n_allocations = (mcfg->header.length - sizeof(acpi::SDTHeader) - 8) / sizeof(acpi::Mcfg::Allocation);
    for(size_t i = 0; i < n_allocations; i++) {
        const auto c = mcfg->allocations[i];
        
        if((c.segment == seg) && (bus >= c.start_bus) && (bus <= c.end_bus))
            return c;
    }

    PANIC("Couldn't find MCFG Allocation");
}

static void parse_bus(const acpi::Mcfg::Allocation& allocation, uint8_t bus);

static void parse_function(const acpi::Mcfg::Allocation& allocation, uint8_t bus, uint8_t slot, uint8_t func) {
    static bool is_on_legacy_bridge = false;
    static pci::RequesterID legacy_bridge_req_id{.raw = 0};
    
    auto addr = get_mcfg_device_addr(allocation, bus, slot, func);

    pci::RequesterID req_id{.raw = 0};
    req_id.bus = bus;
    req_id.slot = slot;
    req_id.func = func;
    if(is_on_legacy_bridge)
        req_id = legacy_bridge_req_id;

    pci::Device dev{.seg = allocation.segment, .bus = bus, .slot = slot, .func = func, .mmio_base = addr, .requester_id = req_id};
    if(dev.read<uint16_t>(0) == 0xFFFF)
        return;

    auto class_code = dev.read<uint8_t>(11);
    auto subclass_code = dev.read<uint8_t>(10);

    if(class_code == 6 && subclass_code == 4)
        dev.bridge_type = pci::BridgeType::PCI_to_PCIe; // Assume PCI-to-PCIe if no PCIe cap

    auto status = dev.read<uint16_t>(6);
    if(status & (1 << 4)) { // Capability list
        auto next = dev.read<uint8_t>(0x34);

        while(next) {
            auto id = dev.read<uint8_t>(next);

            switch (id) {
                case pci::power::id: {
                    dev.power.supported = true;
                    dev.power.offset = next;

                    pci::power::Cap cap{.raw = dev.read<uint16_t>(next + pci::power::cap)};
                    pci::power::Control control{.raw = dev.read<uint16_t>(next + pci::power::control)};

                    dev.power.supported_states = (1 << 0) | (1 << 3) | (cap.d1_support << 1) | (cap.d2_support << 2);
                    dev.power.state = control.power_state;
                    break;
                }
                case pci::msi::id:
                    dev.msi.supported = true;
                    dev.msi.offset = next;
                    break;
                case pci::msix::id: {
                    dev.msix.supported = true;
                    dev.msix.offset = next;

                    pci::msix::Control control{.raw = dev.read<uint16_t>(next + pci::msix::control)};
                    pci::msix::Addr table{.raw = dev.read<uint32_t>(next + pci::msix::table)};
                    pci::msix::Addr pending{.raw = dev.read<uint32_t>(next + pci::msix::pending)};

                    dev.msix.n_messages = control.n_irqs + 1;

                    dev.msix.table.bar = table.bir;
                    dev.msix.table.offset = table.offset << 3;

                    dev.msix.pending.bar = pending.bir;
                    dev.msix.pending.offset = pending.offset << 3;
                    break;
                }
                
                case pci::pcie::id:
                    dev.pcie.found = true;
                    dev.pcie.offset = next;

                    auto type = (dev.read<uint16_t>(next + 2) >> 4) & 0xF;

                    switch(type) {
                    case 0b100: // PCIe Root Complex Root Port
                        dev.bridge_type = pci::BridgeType::PCIe_to_PCIe;
                        break;
                    case 0b1000: // PCI/PCI-X to PCIe Bridge
                        dev.bridge_type = pci::BridgeType::PCI_to_PCIe;
                        break;

                    case 0b1001: [[fallthrough]]; // PCIe Root Complex Integrated Endpoint
                    case 0: [[fallthrough]]; // PCIe Endpoint
                    case 1: // Legacy PCIe Endpoint
                        break;
                    
                    default:
                        print("pci: Unknown PCIe cap type {:#b}\n", type);
                        break;
                    }
                    break;
            }

            next = dev.read<uint8_t>(next + 1);
        }
    }

    // PCI-to-PCI bridge
    if(class_code == 6 && subclass_code == 4) {
        auto prev_status = is_on_legacy_bridge;
        auto prev_req_id = legacy_bridge_req_id;

        ASSERT(dev.bridge_type != pci::BridgeType::None);
        if(dev.bridge_type == pci::BridgeType::PCI_to_PCIe) {
            is_on_legacy_bridge = true;
            legacy_bridge_req_id = req_id;
        }

        parse_bus(allocation, dev.read<uint8_t>(0x19));

        is_on_legacy_bridge = prev_status;
        legacy_bridge_req_id = prev_req_id;
    }

    devices.emplace_back(std::move(dev));
}

static void parse_slot(const acpi::Mcfg::Allocation& allocation, uint8_t bus, uint8_t slot) {
    auto addr = get_mcfg_device_addr(allocation, bus, slot, 0);
    pci::Device dev{.seg = allocation.segment, .bus = bus, .slot = slot, .func = 0, .mmio_base = addr, .requester_id = {.raw = 0}};
    if(dev.read<uint16_t>(0) == 0xFFFF)
        return;

    size_t n_functions = 1;
    if(dev.read<uint8_t>(0xE) & (1 << 7))
        n_functions = 8;

    for(size_t i = 0; i < n_functions; i++)
        parse_function(allocation, bus, slot, i);
}

static void parse_bus(const acpi::Mcfg::Allocation& allocation, uint8_t bus) {
    for(size_t i = 0; i < 32; i++)
        parse_slot(allocation, bus, i);
}

size_t eval_aml_method(lai_nsnode_t* node, const char* name, lai_state_t* state) {
    ASSERT(node);
    ASSERT(name);
    ASSERT(state);

    LAI_CLEANUP_VAR lai_variable_t var = {};
    auto* handle = lai_resolve_path(node, name);
    if(!handle)
        return 0;

    if(auto e = lai_eval(&var, handle, state); e != LAI_ERROR_NONE)
        PANIC("Failed to evaluate PCI AML method");

    uint64_t ret = 0;
    if(auto e = lai_obj_get_integer(&var, &ret); e != LAI_ERROR_NONE)
        PANIC("Failed to get integer from PCI AML method return");

    return ret;
}

extern "C" uintptr_t _pci_drivers_start;
extern "C" uintptr_t _pci_drivers_end;

void pci::init() {
    auto* mcfg = acpi::get_table<acpi::Mcfg>();
    ASSERT(mcfg);

    LAI_CLEANUP_STATE lai_state_t state;
    lai_init_state(&state);

    LAI_CLEANUP_VAR lai_variable_t pci_pnp_id = {};
    LAI_CLEANUP_VAR lai_variable_t pcie_pnp_id = {};
    lai_eisaid(&pci_pnp_id, ACPI_PCI_ROOT_BUS_PNP_ID);
    lai_eisaid(&pcie_pnp_id, ACPI_PCIE_ROOT_BUS_PNP_ID);

    auto* _SB_ = lai_resolve_path(nullptr, "_SB_");
    ASSERT(_SB_);

    struct lai_ns_child_iterator it = LAI_NS_CHILD_ITERATOR_INITIALIZER(_SB_);
    lai_nsnode_t* node;
    while((node = lai_ns_child_iterate(&it))) {
        // Actually is the check for the PCIe really needed, the old PCI id is in _CID after all
        if(lai_check_device_pnp_id(node, &pci_pnp_id, &state) && lai_check_device_pnp_id(node, &pcie_pnp_id, &state))
            continue;

        auto seg = eval_aml_method(node, "_SEG", &state);
        auto bbn = eval_aml_method(node, "_BBN", &state);
        auto allocation = get_mcfg_allocation(mcfg, seg, bbn);

        parse_bus(allocation, bbn);
    }

    // Set all devices to D0
    for(auto& device : devices) {
        bool success = device.set_power(0);
        if(!success)
            print("pci: Failed to set {}:{}:{}.{} to D0\n", device.seg, (uint64_t)device.bus, (uint64_t)device.slot, (uint64_t)device.func);
    }

    auto* start = (Driver**)&_pci_drivers_start;
    auto* end = (Driver**)&_pci_drivers_end;
    size_t size = end - start;
    auto find = [&](pci::Device& dev) -> Driver* {
        for(size_t i = 0; i < size; i++) {
            auto& driver = *start[i];

            if(driver.match == 0)
                continue;

            if(driver.match & match::class_code && driver.class_code != dev.read<uint8_t>(11))
                continue;

            if(driver.match & match::subclass_code && driver.subclass_code != dev.read<uint8_t>(10))
                continue;

            if(driver.match & match::protocol_code && driver.protocol_code != dev.read<uint8_t>(9))
                continue;

            if(driver.match & match::vendor_device) {
                bool found = false;
                for(const auto& [vid, did] : driver.id_list) {
                    if(dev.read<uint16_t>(0) == vid && dev.read<uint16_t>(2) == did) {
                        found = true;
                        break;
                    }
                }
                
                if(!found)
                    continue;
            }

            
            return &driver;
        }

        return nullptr;
    };

    print("pci: Enumerated devices:\n");
    for(auto& device : devices) {
        print("   - {:x}:{:x}:{:x}.{:x} - {:x}:{:x} {:x}.{:x}.{:x}", device.seg, (uint64_t)device.bus, (uint64_t)device.slot, (uint64_t)device.func, device.read<uint16_t>(0), device.read<uint16_t>(2), (uint64_t)device.read<uint8_t>(11), (uint64_t)device.read<uint8_t>(10), (uint64_t)device.read<uint8_t>(9));

        if(device.power.supported)
            print(" Power{{{}{}{}{}}}", (device.power.supported_states & (1 << 0)) ? "D0, " : "", (device.power.supported_states & (1 << 1)) ? "D1, " : "", (device.power.supported_states & (1 << 2)) ? "D2, " : "", (device.power.supported_states & (1 << 3)) ? "D3" : "");
        if(device.msi.supported)
            print(" MSI");
        if(device.msix.supported)
            print(" MSI-X");
        if(device.pcie.found)
            print(" PCIe");

        auto* driver = find(device);
        if(driver) {
            device.driver = driver;
            print(" [{}]", driver->name);
        }
        
        print("\n");
    }
}

void pci::handoff_bios() {
    for(auto& device : devices) {
        if(!device.driver)
            continue;

        if(device.driver->bios_handoff)
            device.driver->bios_handoff(device);
    }
}

void pci::init_drivers() {
    for(auto& device : devices) {
        if(!device.driver)
            continue;

        ASSERT(device.driver->init);
        device.driver->init(device);
    }
}

pci::Device* pci::device_by_class(uint8_t class_code, uint8_t subclass_code, uint8_t prog_if, size_t i) {
    size_t curr = 0;
    for(auto& device : devices) {
        if(device.read<uint8_t>(11) == class_code && subclass_code == device.read<uint8_t>(10) && prog_if == device.read<uint8_t>(9)) {
            if(curr != i)
                curr++;
            else
                return &device;
        }
    }
    
    return nullptr;
}

pci::Device* pci::device_by_id(uint16_t vid, uint16_t did, size_t i) {
    size_t curr = 0;
    for(auto& device : devices) {
        if(device.read<uint16_t>(0) == vid && device.read<uint16_t>(2) == did) {
            if(curr != i)
                curr++;
            else
                return &device;
        }
    }
    
    return nullptr;
}

pci::Device* pci::device_by_location(uint16_t seg, uint8_t bus, uint8_t slot, uint8_t func) {
    for(auto& device : devices)
        if(device.seg == seg && device.bus == bus && device.slot == slot && device.func == func)
            return &device;
    return nullptr;
}

uint32_t pci::read_raw(uint16_t seg, uint8_t bus, uint8_t slot, uint8_t func, size_t offset, size_t width) {
    ASSERT(offset < pmm::block_size);

    auto* mcfg = acpi::get_table<acpi::Mcfg>();
    ASSERT(mcfg);

    const auto alloc = get_mcfg_allocation(mcfg, seg, bus);
    auto addr = get_mcfg_device_addr(alloc, bus, slot, func);

    if(width == 4) return *(volatile uint32_t*)(addr + offset);
    else if(width == 2) return *(volatile uint16_t*)(addr + offset);
    else if(width == 1) return *(volatile uint8_t*)(addr + offset);
    else PANIC("Invalid width");
}

void pci::write_raw(uint16_t seg, uint8_t bus, uint8_t slot, uint8_t func, size_t offset, uint32_t v, size_t width) {
    ASSERT(offset < pmm::block_size);

    auto* mcfg = acpi::get_table<acpi::Mcfg>();
    ASSERT(mcfg);
    
    const auto alloc = get_mcfg_allocation(mcfg, seg, bus);
    auto addr = get_mcfg_device_addr(alloc, bus, slot, func);

    if(width == 4) *(volatile uint32_t*)(addr + offset) = v;
    else if(width == 2) *(volatile uint16_t*)(addr + offset) = v & 0xFFFF;
    else if(width == 1) *(volatile uint8_t*)(addr + offset) = v & 0xFF;
    else PANIC("Invalid width");
}

static void install_msix(pci::Device& device, uint16_t index, uint8_t vector) {
    pci::msix::Control control{.raw = device.read<uint16_t>(device.msix.offset + pci::msix::control)};
    control.mask = 1;
    control.enable = 1;
    device.write<uint16_t>(device.msix.offset + pci::msix::control, control.raw);

    ASSERT(index < device.msix.n_messages);

    auto table_bar = device.read_bar(device.msix.table.bar);
    auto base = table_bar.base + device.msix.table.offset;
    ASSERT(base && table_bar.type == pci::Bar::Type::Mmio);

    vmm::get_kernel_context().map(base, base + phys_mem_map, paging::mapPagePresent | paging::mapPageWrite, msr::pat::uc); // TODO: How should this interact with device drivers?
    volatile auto* table = (pci::msix::Entry*)(base + phys_mem_map);

    pci::msi::Data data{};
    data.vector = vector;
    data.delivery_mode = 0;

    pci::msi::Address address{};
    address.base_address = 0xFEE;
    address.destination_id = get_cpu().lapic_id;

    pci::msix::VectorControl vector_control{};
    vector_control.mask = 0;

    table[index].addr_low = address.raw;
    table[index].addr_high = 0; // TODO: High address
    table[index].data = data.raw;
    table[index].control = vector_control.raw;

    control.raw = device.read<uint16_t>(device.msix.offset + pci::msix::control);
    control.mask = 0;
    device.write<uint16_t>(device.msix.offset + pci::msix::control, control.raw);
}

static void install_msi(pci::Device& device, uint16_t index, uint8_t vector) {
    ASSERT(index == 0); // TODO: Support Multiple Message MSI

    pci::msi::Control control{};
    pci::msi::Data data{};
    pci::msi::Address address{};

    control.raw = device.read<uint16_t>(device.msi.offset + pci::msi::control);
    ASSERT((1 << control.mmc) < 32); // Assert count is sane

    //address.raw = device.read<uint32_t>(device.msi.offset + pci::msi::addr);
    //data.raw = device.read<uint32_t>(device.msi.offset + (control.c64 ? pci::msi::data_64 : pci::msi::data_32));

    data.vector = vector;
    data.delivery_mode = 0;

    address.base_address = 0xFEE;
    address.destination_id = get_cpu().lapic_id;

    device.write<uint32_t>(device.msi.offset + pci::msi::addr, address.raw);
    device.write<uint32_t>(device.msi.offset + (control.c64 ? pci::msi::data_64 : pci::msi::data_32), data.raw);

    control.enable = 1;
    control.mme = 0; // Enable 1 IRQ

    device.write<uint16_t>(device.msi.offset + pci::msi::control, control.raw);
}

void pci::Device::enable_irq(uint16_t index, uint8_t vector) {
    if(msix.supported)
        install_msix(*this, index, vector);
    else if(msi.supported)
        install_msi(*this, index, vector);
    else
        PANIC("No IRQ routing support");
}

bool pci::Device::set_power(uint8_t state) {
    if(state > 3)
        return false;
    
    if(!power.supported)
        return (state == 0); // If no PM Cap, only return true if state 0

    if(power.state == state)
        return true;

    if((power.supported_states & (1 << state)) == 0)
        return false;

    // PCI Bus Power Management Interface Specification 1.1 Paragraph 5.6.1
    TimePoint delay{};
    if((power.state == 0 || power.state == 1) && state == 2)
        delay = 200_us;
    else if(state == 3)
        delay = 10_ms;
    else if(power.state == 2 && state == 0)
        delay = 200_us;
    else if(power.state == 3 && state == 0)
        delay = 10_ms;

    pci::power::Control control{.raw = read<uint16_t>(power.offset + pci::power::control)};
    control.power_state = state;
    write<uint16_t>(power.offset + pci::power::control, control.raw);

    tsc::poll_sleep(delay);

    control.raw = read<uint16_t>(power.offset + pci::power::control);

    // Device successfully set state
    if(control.power_state == state) {
        power.state = state;
        return true;
    } else {
        return false;
    }
}

pci::Bar pci::Device::read_bar(size_t i) const {
    if(i > 5)
        return pci::Bar{.type = pci::Bar::Type::Invalid, .base = 0, .len = 0};

    uint32_t off = (0x10 + (i * 4));

    auto bar = read<uint32_t>(off);
    if(bar & 1) { // IO Space
        uint16_t base = bar & 0xFFFC;

        write<uint32_t>(off, ~0);
        uint16_t len = (~((read<uint32_t>(off) & ~3)) + 1) & 0xFFFF;
        write<uint32_t>(off, bar);

        return pci::Bar{.type = pci::Bar::Type::Pio, .base = base, .len = len};
    } else { // MMIO
        uint8_t type = (bar >> 1) & 3;
        uint64_t base = 0;
        if(type == 0) // 32-bit
            base = bar & 0xFFFFFFF0;
        else if(type == 2) // 64-bit
            base = (bar & 0xFFFFFFF0) | ((uint64_t)read<uint32_t>(off + 4) << 32);
        else
            PANIC("Unknown MMIO Bar Type");

        write<uint32_t>(off, ~0);
        uint64_t len = ~((read<uint32_t>(off) & ~0xF)) + 1;
        write<uint32_t>(off, bar);

        return pci::Bar{.type = pci::Bar::Type::Mmio, .base = base, .len = len};
    }
}

void pci::Device::set_privileges(uint8_t privilege) {
    uint16_t command = read<uint16_t>(4);
    command &= ~0b111; // Clear privileges
    command |= privilege; // Set the privileges
    write<uint16_t>(4, command);
}