#pragma once

#include <Luna/common.hpp>

#include <Luna/drivers/pci.hpp>

#include <Luna/cpu/paging.hpp>

namespace iommu
{
    void init();
    void map(const pci::Device& device, uintptr_t pa, uintptr_t iova, uint64_t flags);
    uintptr_t unmap(const pci::Device& device, uintptr_t iova);
} // namespace iommu
