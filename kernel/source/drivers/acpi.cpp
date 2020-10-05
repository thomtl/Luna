#include <Luna/drivers/acpi.hpp>

#include <Luna/mm/vmm.hpp>
#include <Luna/mm/hmm.hpp>

#include <Luna/misc/format.hpp>

#include <std/vector.hpp>

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

static std::vector<acpi::SDTHeader*> acpi_tables;

void acpi::init(const stivale2::Parser& parser) {
    auto& vmm = vmm::kernel_vmm::get_instance();

    auto rsdp_phys = (uintptr_t)parser.acpi_rsdp();
    vmm.map(rsdp_phys, rsdp_phys + phys_mem_map, paging::mapPagePresent);
    auto* rsdp = (Rsdp*)(rsdp_phys + phys_mem_map);
    ASSERT(do_checksum(rsdp, sizeof(Rsdp)));

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
}

void* acpi::get_table(const char* sig, size_t index) {
    size_t n = 0;
    for(auto* h : acpi_tables) {
        if(strncmp(h->sig, sig, 4) == 0) {
            if(n != index)
                n++;
            else
                return (void*)h;
        }
    }

    PANIC("Couldn't find table");
}