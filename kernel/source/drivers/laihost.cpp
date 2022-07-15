#include <Luna/common.hpp>

#include <Luna/cpu/pio.hpp>
#include <Luna/cpu/tsc.hpp>

#include <Luna/drivers/acpi.hpp>
#include <Luna/drivers/pci.hpp>

#include <Luna/mm/vmm.hpp>
#include <Luna/mm/hmm.hpp>

#include <Luna/misc/log.hpp>

#include <lai/core.h>
#include <lai/host.h>

extern "C" {
    void* laihost_malloc(size_t s) {
        return (void*)hmm::alloc(s, 16);
    }

    void* laihost_realloc(void* ptr, size_t s, size_t) {
        return (void*)hmm::realloc((uintptr_t)ptr, s, 16);
    }

    void laihost_free(void* ptr, size_t) {
        hmm::free((uintptr_t)ptr);
    }

    void laihost_log(int level, const char* msg) {
        print("lai: {:s}: {:s}\n", (level == LAI_DEBUG_LOG) ? "Debug" : "Warning", msg);
    }

    void laihost_panic(const char* msg) {
        PANIC(msg);
    }

    void* laihost_scan(const char* signature, size_t index) {
        return acpi::get_table(signature, index);
    }

    void* laihost_map(uintptr_t addr, size_t bytes) {
        size_t n_pages = div_ceil(bytes, pmm::block_size);

        auto& vmm = vmm::KernelVmm::get_instance();

        for(size_t i = 0; i < n_pages; i++)
            vmm.map(addr + (i * pmm::block_size), addr + (i * pmm::block_size) + phys_mem_map, paging::mapPagePresent | paging::mapPageWrite);

        return (void*)(addr + phys_mem_map);
    }

    void laihost_unmap([[maybe_unused]] void* addr, [[maybe_unused]] size_t bytes) {}

    void laihost_outb(uint16_t port, uint8_t val) { pio::outb(port, val); }
    void laihost_outw(uint16_t port, uint16_t val) { pio::outw(port, val); }
    void laihost_outd(uint16_t port, uint32_t val) { pio::outd(port, val); }

    uint8_t laihost_inb(uint16_t port) { return pio::inb(port); }
    uint16_t laihost_inw(uint16_t port) { return pio::inw(port); }
    uint32_t laihost_ind(uint16_t port) { return pio::ind(port); }

    void laihost_pci_writeb(uint16_t seg, uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset, uint8_t value) { pci::write<uint8_t>(seg, bus, slot, func, offset, value); }
    void laihost_pci_writew(uint16_t seg, uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset, uint16_t value) { pci::write<uint16_t>(seg, bus, slot, func, offset, value); }
    void laihost_pci_writed(uint16_t seg, uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset, uint32_t value) { pci::write<uint32_t>(seg, bus, slot, func, offset, value); }

    uint8_t laihost_pci_readb(uint16_t seg, uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset) { return pci::read<uint8_t>(seg, bus, slot, func, offset); }
    uint16_t laihost_pci_readw(uint16_t seg, uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset) { return pci::read<uint16_t>(seg, bus, slot, func, offset); }
    uint32_t laihost_pci_readd(uint16_t seg, uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset) { return pci::read<uint32_t>(seg, bus, slot, func, offset); }
    
    void laihost_sleep(uint64_t ms) {
        tsc::poll_msleep(ms);
    }

    uint64_t laihost_timer() {
        return tsc::time_ns() / 100; // 100ns increments
    }

    int laihost_sync_wait(lai_sync_state* state, unsigned int, int64_t) { // TODO: Use threads to actually implement this and wake, some stupid hardware relies on the timeout too instead of just unlocking its stuff
        state->val &= ~3;
        return 0;
    }

}