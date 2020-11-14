#include <Luna/drivers/iommu.hpp>
#include <std/utility.hpp>

#include <Luna/drivers/intel/vt_d.hpp>

enum class Vendor {
    Intel
};

Vendor vendor;
std::lazy_initializer<vt_d::IOMMU> intel_iommu;

void iommu::init() {
    if(vt_d::has_iommu()) {
        vendor = Vendor::Intel;
        intel_iommu.init();
    } else {
        PANIC("Unknown vendor for IOMMU");
    }
}

void iommu::map(const pci::Device& device, uintptr_t pa, uintptr_t iova, uint64_t flags) {
    if(vendor == Vendor::Intel)
        intel_iommu->map(device, pa, iova, flags);
    else
        PANIC("Unknown vendor");
}

uintptr_t iommu::unmap(const pci::Device& device, uintptr_t iova) {
    if(vendor == Vendor::Intel)
        return intel_iommu->unmap(device, iova);
    else
        PANIC("Unknown vendor");
}