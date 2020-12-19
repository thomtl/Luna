#include <Luna/cpu/amd/asid.hpp>

svm::AsidManager::AsidManager(uint32_t n_asids): _asids{n_asids} {
    _asids.set(0); // Reserve ASID 0
}
uint32_t svm::AsidManager::alloc() {
    auto asid = _asids.get_free_bit();
    if(asid == ~0u)
        return ~0;

    _asids.set(asid);
    return asid;
}
        
void svm::AsidManager::free(uint32_t asid) {
    ASSERT(_asids.test(asid));

    _asids.clear(asid);
}

void svm::invlpga(uint32_t asid, uintptr_t va) {
    asm volatile("invlpga %[Address], %[Asid]" : : [Asid] "c"(asid), [Address] "a"(va) : "memory");
}