
#include <Luna/drivers/pci.hpp>

#include <Luna/cpu/tsc.hpp>

#include <Luna/mm/vmm.hpp>
#include <Luna/misc/log.hpp>

extern "C" {
    #include "../../../../subprojects/lil/src/imports.h"

    void lil_pci_writeb(void* device, uint16_t offset, uint8_t val) { ((pci::Device*)device)->write<uint8_t>(offset, val); }
    void lil_pci_writew(void* device, uint16_t offset, uint16_t val) { ((pci::Device*)device)->write<uint16_t>(offset, val); }
    void lil_pci_writed(void* device, uint16_t offset, uint32_t val) { ((pci::Device*)device)->write<uint32_t>(offset, val); }

    uint8_t lil_pci_readb(void* device, uint16_t offset) { return ((pci::Device*)device)->read<uint8_t>(offset); }
    uint16_t lil_pci_readw(void* device, uint16_t offset) { return ((pci::Device*)device)->read<uint16_t>(offset); }
    uint32_t lil_pci_readd(void* device, uint16_t offset) { return ((pci::Device*)device)->read<uint32_t>(offset); }

    void lil_sleep(uint64_t ms) { tsc::poll_msleep(ms); }
    void lil_panic(const char* msg) {
        print("lil: Panic: {}\n", msg);
        PANIC("LIL PANIC");
    }

    void* lil_malloc(size_t s) { return (void*)hmm::alloc(s, 16); }
    void free(void* p) { hmm::free((uintptr_t)p); }

    void* lil_map(size_t addr, size_t bytes) {
        size_t n_pages = div_ceil(bytes, pmm::block_size);

        auto& vmm = vmm::KernelVmm::get_instance();

        for(size_t i = 0; i < n_pages; i++)
            vmm.map(addr + (i * pmm::block_size), addr + (i * pmm::block_size) + phys_mem_map, paging::mapPagePresent | paging::mapPageWrite);

        return (void*)(addr + phys_mem_map);
    }
}