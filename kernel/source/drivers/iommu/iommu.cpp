#include <Luna/drivers/iommu/iommu.hpp>
#include <std/utility.hpp>

#include <Luna/drivers/iommu/intel/vt_d.hpp>
#include <Luna/drivers/iommu/amd/amd_vi.hpp>

CpuVendor vendor;

union {
    std::lazy_initializer<vt_d::IOMMU> intel_iommu;
    std::lazy_initializer<amd_vi::IOMMU> amd_iommu;
} mmu;

void iommu::init() {
    if(vt_d::has_iommu()) {
        vendor = CpuVendor::Intel;
        mmu.intel_iommu.init();
    } else if(amd_vi::has_iommu()) {
        vendor = CpuVendor::AMD;
        mmu.amd_iommu.init();
    } else {
        PANIC("Unknown vendor for IOMMU");
    }
}

void iommu::map(const pci::Device& device, uintptr_t pa, uintptr_t iova, uint64_t flags) {
    if(vendor == CpuVendor::Intel)
        mmu.intel_iommu->map(device, pa, iova, flags);
    else if(vendor == CpuVendor::AMD)
        mmu.amd_iommu->map(device, pa, iova, flags);
    else
        PANIC("Unknown vendor");
}

uintptr_t iommu::unmap(const pci::Device& device, uintptr_t iova) {
    if(vendor == CpuVendor::Intel)
        return mmu.intel_iommu->unmap(device, iova);
    else if(vendor == CpuVendor::AMD)
        return mmu.amd_iommu->unmap(device, iova);
    else
        PANIC("Unknown vendor");
}