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
    for(auto& device : devices)
        print("   - {}:{}:{}.{} - {:x}:{:x} {}.{}.{}\n", device.seg, (uint64_t)device.bus, (uint64_t)device.slot, (uint64_t)device.func, device.read<uint16_t>(0), device.read<uint16_t>(2), (uint64_t)device.read<uint8_t>(9), (uint64_t)device.read<uint8_t>(10), (uint64_t)device.read<uint8_t>(11));
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