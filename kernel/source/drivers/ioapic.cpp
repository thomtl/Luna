#include <Luna/drivers/ioapic.hpp>

#include <Luna/cpu/cpu.hpp>
#include <Luna/cpu/idt.hpp>

#include <Luna/drivers/acpi.hpp>

#include <Luna/mm/vmm.hpp>

#include <std/vector.hpp>

uint32_t ioapic::Ioapic::read(uint32_t i) {
    *(volatile uint32_t*)mmio_base = i;
    return *(volatile uint32_t*)(mmio_base + 0x10);
}

void ioapic::Ioapic::write(uint32_t i, uint32_t v) {
    *(volatile uint32_t*)mmio_base = i;
    *(volatile uint32_t*)(mmio_base + 0x10) = v;
}

void ioapic::Ioapic::set_entry(uint32_t i, uint64_t v) {
    write(regs::entry(i), v & 0xFFFF'FFFF);
    write(regs::entry(i) + 1, (v >> 32) & 0xFFFF'FFFF);
}

uint64_t ioapic::Ioapic::read_entry(uint32_t i) {
    return read(regs::entry(i)) | ((uint64_t)read(regs::entry(i) + 1) << 32);
}

ioapic::Ioapic::Ioapic(uintptr_t pa, uint32_t gsi_base): gsi_base{gsi_base} {
    mmio_base = pa + phys_mem_map;
    vmm::KernelVmm::get_instance().map(pa, mmio_base, paging::mapPagePresent | paging::mapPageWrite);

    n_redirection_entries = ((read(regs::version) >> 16) & 0xFF) + 1;

    for(size_t i = 0; i < n_redirection_entries; i++)
        mask(i);
}

void ioapic::Ioapic::set(uint8_t i, uint8_t vector, regs::DeliveryMode delivery, regs::DestinationMode dest, uint16_t flags, uint32_t lapic_id) {
    uint64_t v = 0;
    v |= vector;
    v |= ((uint8_t)delivery << 8);
    v |= ((uint8_t)dest << 11);

    if(flags & 0x2)
        v |= (1 << 13);

    if(flags & 0x8)
        v |= (1 << 15);

    v |= ((uint64_t)lapic_id << 56);
    set_entry(i, v);
}

void ioapic::Ioapic::mask(uint8_t i) {
    set_entry(i, read_entry(i) | (1 << 16));
}

void ioapic::Ioapic::unmask(uint8_t i) {
    set_entry(i, read_entry(i) & ~(1 << 16));
}

std::vector<ioapic::Ioapic> ioapics;

void ioapic::init() {
    acpi::MadtParser madt{};

    auto ioapic_entries = madt.get_entries_of_type<acpi::IoapicMadtEntry>();
    for(const auto entry : ioapic_entries)
        ioapics.emplace_back(entry.addr, entry.gsi_base);

    auto isos = madt.get_entries_of_type<acpi::ISOMadtEntry>();
    if(madt.has_legacy_pic()) {
        for(size_t i = 0; i < 16; i++) {
            if(i == 2)
                continue;

            bool found = false;
            for(const auto iso : isos) {
                if(iso.src == i) {
                    set(iso.gsi, iso.src + 0x20, regs::DeliveryMode::Fixed, regs::DestinationMode::Physical, iso.flags, get_cpu().lapic_id);
                    idt::reserve_vector(iso.src + 0x20);
                    mask(iso.gsi);
                    found = true;
                    break;
                }
            }

            if(!found) {
                set(i, i + 0x20, regs::DeliveryMode::Fixed, regs::DestinationMode::Physical, 0, get_cpu().lapic_id);
                idt::reserve_vector(i + 0x20);
                mask(i);
            }
        }
    }
}

ioapic::Ioapic& get_ioapic_for_gsi(uint32_t gsi) {
    for(auto& ioapic : ioapics) {
        const auto [start, end] = ioapic.gsi_range();
        if(start <= gsi && end >= gsi)
            return ioapic;
    }
    
    PANIC("Couldn't find ioapic");
}

void ioapic::set(uint32_t gsi, uint8_t vector, ioapic::regs::DeliveryMode delivery, ioapic::regs::DestinationMode dest, uint16_t flags, uint32_t lapic_id) {
    auto& ioapic = get_ioapic_for_gsi(gsi);
    ioapic.set(gsi - ioapic.gsi_range().first, vector, delivery, dest, flags, lapic_id);
}

void ioapic::mask(uint32_t gsi) {
    auto& ioapic = get_ioapic_for_gsi(gsi);
    ioapic.mask(gsi - ioapic.gsi_range().first);
}

void ioapic::unmask(uint32_t gsi) {
    auto& ioapic = get_ioapic_for_gsi(gsi);
    ioapic.unmask(gsi - ioapic.gsi_range().first);
}