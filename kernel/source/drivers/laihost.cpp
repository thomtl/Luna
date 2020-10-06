#include <Luna/common.hpp>

#include <Luna/cpu/pio.hpp>

#include <Luna/drivers/acpi.hpp>

#include <Luna/mm/vmm.hpp>
#include <Luna/mm/hmm.hpp>

#include <Luna/misc/format.hpp>

#include <lai/core.h>
#include <lai/host.h>

extern "C" {
    void* laihost_malloc(size_t s) {
        return (void*)hmm::alloc(s, 16);
    }

    void* laihost_realloc(void* ptr, size_t s) {
        return (void*)hmm::realloc((uintptr_t)ptr, s, 16);
    }

    void laihost_free(void* ptr) {
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

        auto& vmm = vmm::kernel_vmm::get_instance();

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


    // TODO: PCI functions, Sleeping
}