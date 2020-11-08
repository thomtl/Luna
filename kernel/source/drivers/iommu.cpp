#include <Luna/drivers/iommu.hpp>
#include <std/utility.hpp>

#include <Luna/cpu/cpu.hpp>

#include <Luna/drivers/intel/vt_d.hpp>

enum class Vendor {
    Intel
};

Vendor vendor;
std::lazy_initializer<vt_d::IOMMU> intel_iommu;

void iommu::init() {
    uint32_t a, b, c, d;
    if(!cpu::cpuid(0, a, b, c, d))
        PANIC("CPUID leaf 0 does not exist");

    if(b == cpu::signature_intel_ebx && c == cpu::signature_intel_ecx && d == cpu::signature_intel_edx) {
        vendor = Vendor::Intel;
        intel_iommu.init();
    } else {
        PANIC("Unknown vendor for IOMMU");
    }
}

void iommu::map(const pci::Device& device, uintptr_t pa, uintptr_t iova, uint64_t flags) {
    if(vendor == Vendor::Intel)
        return intel_iommu->get_translation(device).map(pa, iova, flags);
    else
        PANIC("Unknown vendor");
}