#include <Luna/drivers/pci.hpp>
#include <Luna/drivers/acpi.hpp>
#include <Luna/mm/vmm.hpp>

#include <Luna/misc/format.hpp>

#include <lai/core.h>
#include <acpispec/hw.h>

static std::vector<pci::Device> devices; 

static uintptr_t get_mcfg_device_addr(const acpi::Mcfg::Allocation& allocation, uint8_t bus, uint8_t slot, uint8_t func) {
    auto pa = allocation.base + (((bus - allocation.start_bus) << 20) | (slot << 15) | (func << 12));
    auto va = pa + phys_mem_map;
    vmm::kernel_vmm::get_instance().map(pa, va, paging::mapPagePresent | paging::mapPageWrite);

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

static void parse_function(const acpi::Mcfg::Allocation& allocation, uint8_t bus, uint8_t slot, uint8_t func) {
    auto addr = get_mcfg_device_addr(allocation, bus, slot, func);

    pci::Device dev{.seg = allocation.segment, .bus = bus, .slot = slot, .func = func, .mmio_base = addr};
    if(dev.read<uint16_t>(0) == 0xFFFF)
        return;

    auto status = dev.read<uint16_t>(6);
    if(status & (1 << 4)) { // Capability list
        auto next = dev.read<uint8_t>(0x34);

        while(next) {
            auto id = dev.read<uint8_t>(next);

            switch (id) {
                case pci::msi::id:
                    dev.msi.supported = true;
                    dev.msi.offset = next;
                    break;
                case pci::msix::id:
                    dev.msix.supported = true;
                    dev.msix.offset = next;
                    break;
            }

            next = dev.read<uint8_t>(next + 1);
        }
    }

    devices.emplace_back(std::move(dev));
}

static void parse_slot(const acpi::Mcfg::Allocation& allocation, uint8_t bus, uint8_t slot) {
    auto addr = get_mcfg_device_addr(allocation, bus, slot, 0);
    pci::Device dev{.seg = allocation.segment, .bus = bus, .slot = slot, .func = 0, .mmio_base = addr};
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

    print("pci: Enumerated devices:\n");
    for(auto& device : devices) {
        print("   - {}:{}:{}.{} - {:x}:{:x} {}.{}.{}", device.seg, (uint64_t)device.bus, (uint64_t)device.slot, (uint64_t)device.func, device.read<uint16_t>(0), device.read<uint16_t>(2), (uint64_t)device.read<uint8_t>(9), (uint64_t)device.read<uint8_t>(10), (uint64_t)device.read<uint8_t>(11));

        if(device.msi.supported)
            print(" MSI");
        if(device.msix.supported)
            print(" MSI-X");
        
        print("\n");
    }
}

pci::Device& pci::device_by_class(uint8_t class_code, uint8_t subclass_code, uint8_t prog_if, size_t i) {
    size_t curr = 0;
    for(auto& device : devices) {
        if(device.read<uint8_t>(9) == class_code && subclass_code == device.read<uint8_t>(10) && device.read<uint8_t>(11) == prog_if) {
            if(curr != i)
                curr++;
            else
                return device;
        }
    }
    
    PANIC("Couldn't find device");
}

pci::Device& pci::device_by_id(uint16_t vid, uint16_t did, size_t i) {
    size_t curr = 0;
    for(auto& device : devices) {
        if(device.read<uint16_t>(0) == vid && device.read<uint16_t>(2) == did) {
            if(curr != i)
                curr++;
            else
                return device;
        }
    }
    
    PANIC("Couldn't find device");
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

static void install_msi(pci::Device& device, uint8_t vector) {
    ASSERT(device.msi.supported);
    ASSERT(device.msi.offset != 0);

    pci::msi::Control control{};
    pci::msi::Data data{};
    pci::msi::Address address{};

    control.raw = device.read<uint16_t>(device.msi.offset + pci::msi::control);
    ASSERT((1 << control.mmc) < 32); // Assert count is sane

    address.raw = device.read<uint32_t>(device.msi.offset + pci::msi::addr);
    data.raw = device.read<uint32_t>(device.msi.offset + (control.c64 ? pci::msi::data_64 : pci::msi::data_32));

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

void pci::Device::enable_irq(uint8_t vector) {
    if(msix.supported)
        PANIC("TODO: Support MSI-X");
    else if(msi.supported)
        install_msi(*this, vector);
    else
        PANIC("No IRQ routing support");
}