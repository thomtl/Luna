#include <Luna/drivers/acpi.hpp>

#include <Luna/mm/vmm.hpp>
#include <Luna/mm/hmm.hpp>

#include <Luna/misc/format.hpp>

#include <std/vector.hpp>

#include <lai/core.h>

static bool do_checksum(const void* addr, size_t size) {
    uint8_t sum = 0;
    for(size_t i = 0; i < size; i++)
        sum += ((const uint8_t*)addr)[i];

    return sum == 0;
}

static bool do_checksum(const acpi::SDTHeader* header) {
    return do_checksum(header, header->length);
}

static acpi::SDTHeader* map_table(uintptr_t pa) {
    auto& vmm = vmm::kernel_vmm::get_instance();

    vmm.map(pa, pa + phys_mem_map, paging::mapPagePresent);
    auto* header = (acpi::SDTHeader*)(pa + phys_mem_map);

    auto addr = pa - pmm::block_size;
    auto n_pages = div_ceil(header->length, pmm::block_size) + 2;
    for(size_t i = 0; i < n_pages; i++) {
        auto pa = addr + (i * pmm::block_size);
        auto va = pa + phys_mem_map;
        vmm.map(pa, va, paging::mapPagePresent);
    }

    return header;
}

static uint8_t revision;
static std::vector<acpi::SDTHeader*> acpi_tables;

void acpi::init(const stivale2::Parser& parser) {
    auto& vmm = vmm::kernel_vmm::get_instance();

    auto rsdp_phys = (uintptr_t)parser.acpi_rsdp();
    vmm.map(rsdp_phys, rsdp_phys + phys_mem_map, paging::mapPagePresent);
    auto* rsdp = (Rsdp*)(rsdp_phys + phys_mem_map);
    ASSERT(do_checksum(rsdp, sizeof(Rsdp)));

    revision = rsdp->revision;

    SDTHeader* tables = nullptr;

    if(rsdp->revision > 0) {
        auto* xsdp = (Xsdp*)rsdp;
        ASSERT(do_checksum(xsdp, sizeof(Xsdp)));
        tables = map_table(xsdp->xsdt);
    } else {
        tables = map_table(rsdp->rsdt);
    }

    ASSERT(do_checksum(tables));

    size_t n_entries = (tables->length - sizeof(SDTHeader)) / ((rsdp->revision > 0) ? 8 : 4);

    print("acpi: {:s} detected, with {:d} tables\n", ((rsdp->revision > 0) ? "ACPI 2 or higher" : "ACPI 1"), n_entries);
    for(size_t i = 0; i < n_entries; i++) {
        auto pa = (rsdp->revision > 0) ? ((Xsdt*)tables)->sdts[i] : ((Rsdt*)tables)->sdts[i];
        auto* h = map_table(pa);
        ASSERT(do_checksum(h));

        acpi_tables.push_back(h);

        print("    - {}{}{}{}: Revision: {:d}, OEMID \"{}{}{}{}{}{}\", at {:#x}\n", h->sig[0], h->sig[1], h->sig[2], h->sig[3], (uint64_t)h->revision, h->oemid[0], h->oemid[1], h->oemid[2], h->oemid[3], h->oemid[4], h->oemid[5], pa);
    }

    lai_set_acpi_revision(rsdp->revision);
    lai_create_namespace();
}

acpi::SDTHeader* acpi::get_table(const char* sig, size_t index) {
    // Only 2 tables are not found in the {R, X}SDT, the DSDT and FACS, which are both found through the FADT, so special case these
    if(strncmp(sig, "DSDT", 4) == 0) {
        auto* fadt = acpi::get_table<acpi::Fadt>();
        uintptr_t dsdt_pa = 0;
        if(fadt->x_dsdt && revision > 0)
            dsdt_pa = fadt->x_dsdt;
        else
            dsdt_pa = fadt->dsdt;
        
        return map_table(dsdt_pa);
    } else if(strncmp(sig, "FACS", 4) == 0) {
        auto* fadt = acpi::get_table<acpi::Fadt>();
        uintptr_t facs_pa = 0;
        if(fadt->x_firmware_ctrl && revision > 0)
            facs_pa = fadt->x_firmware_ctrl;
        else
            facs_pa = fadt->firmware_ctrl;
        
        return map_table(facs_pa);
    } else {
        size_t n = 0;
        for(auto* h : acpi_tables) {
            if(strncmp(h->sig, sig, 4) == 0) {
                if(n != index)
                    n++;
                else
                    return h;
            }
        }
    }
    
    return nullptr;
}