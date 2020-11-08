#pragma once

#include <Luna/common.hpp>

#include <Luna/drivers/pci.hpp>

namespace iommu
{
    void init();
    void map(const pci::Device& device, uintptr_t pa, uintptr_t iova, uint64_t flags);
} // namespace iommu
