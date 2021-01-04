#pragma once

#include <Luna/common.hpp>
#include <std/vector.hpp>
#include <std/string.hpp>

#include <Luna/mm/vmm.hpp>
#include <Luna/drivers/iommu.hpp>

#include <Luna/misc/format.hpp>

namespace iovmm {
    class Iovmm {
        public:
        Iovmm(pci::Device* device): _device{device} {}

        struct Region {
            uintptr_t base;
            size_t len;
        };

        void push_region(Region region) { 
            _regions.push_back(region); 
        }

        struct Allocation {
            uintptr_t guest_base;
            uint8_t* host_base;
            size_t len;
        };

        enum DmaDirection : uint8_t {
            DeviceToHost = (1 << 0),
            HostToDevice = (1 << 1),
            Bidirectional = DeviceToHost | HostToDevice
        };
        
        Allocation alloc(size_t len, uint8_t direction) {
            size_t aligned_len = align_up(len, pmm::block_size);
            for(auto& region : _regions) {
                if(region.len >= aligned_len) {
                    region.len -= aligned_len;

                    auto base = region.base;
                    region.base += aligned_len;

                    auto& kvmm = vmm::kernel_vmm::get_instance();
                    uintptr_t host_region = hmm::alloc(len, pmm::block_size); // Will do its own alignment internally
                    ASSERT(((uintptr_t)host_region & (pmm::block_size - 1)) == 0);
                    for(size_t i = 0; i < aligned_len; i += pmm::block_size)
                        kvmm.set_caching(host_region + i, msr::pat::uc);

                    memset((void*)host_region, 0, len);

                    uint64_t flags = 0;
                    flags |= (direction & HostToDevice) ? paging::mapPagePresent : 0; // Present actually means read here, maybe use different flags for IOMMU paging?
                    flags |= (direction & DeviceToHost) ? paging::mapPageWrite : 0;

                    for(size_t i = 0; i < aligned_len; i += pmm::block_size)
                        iommu::map(*_device, kvmm.get_phys(host_region + i), base + i, flags);

                    return {base, (uint8_t*)host_region, len};
                }
            }

            return {0, nullptr, 0};
        }

        void free(const Allocation& obj) {
            // Unmap region from device and free pages
            size_t aligned_len = align_up(obj.len, pmm::block_size);
            for(size_t i = 0; i < aligned_len; i += pmm::block_size)
                iommu::unmap(*_device, obj.guest_base + i);

            hmm::free((uintptr_t)(obj.host_base));

            // First check if we are adjacent to any other regions
            for(auto& region : _regions) {
                // Underneath it
                if((obj.guest_base + aligned_len) == region.base) {
                    region.base -= aligned_len;
                    region.len += aligned_len;

                    return;
                }

                // Above it
                if((region.base + region.len) == obj.guest_base) {
                    region.len += aligned_len;
                    return;
                }
            }

            // Nope, just push our own entry, might create some adjacent regions that could be merged but its no issue
            _regions.push_back({obj.guest_base, aligned_len});
        }

        private:
        pci::Device* _device;
        std::vector<Region> _regions;
    };
} // namespace iovmm
