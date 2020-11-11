#pragma once

#include <Luna/common.hpp>
#include <std/vector.hpp>
#include <std/string.hpp>

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
            _regions.emplace_back(region.base, region.len); 
        }

        Region alloc(size_t len, uint8_t* write_data = nullptr) {
            size_t aligned_len = align_up(len, pmm::block_size);
            for(auto& region : _regions) {
                if(region.len >= aligned_len) {
                    region.len -= aligned_len;

                    auto base = region.base;
                    region.base += aligned_len;

                    for(size_t i = 0; i < aligned_len; i += pmm::block_size) {
                        auto pa = pmm::alloc_block();
                        ASSERT(pa);

                        memset((void*)(pa + phys_mem_map), 0, pmm::block_size);

                        if(write_data) {
                            size_t remaining = len - i;
                            size_t transfer = (remaining >= 0x1000) ? pmm::block_size : remaining;

                            memcpy((void*)(pa + phys_mem_map), write_data + i, transfer);
                        }

                        iommu::map(*_device, pa, base + i, paging::mapPagePresent | paging::mapPageWrite);
                    }

                    return {base, len};
                }
            }

            return {0, 0};
        }

        void free(Region obj, uint8_t* read_data = nullptr) {
            // Unmap region from device and free pages
            size_t aligned_len = align_up(obj.len, pmm::block_size);
            for(size_t i = 0; i < aligned_len; i += pmm::block_size) {
                auto pa = iommu::unmap(*_device, obj.base + i);
                ASSERT(pa);

                if(read_data) {
                    size_t remaining = obj.len - i;
                    size_t transfer = (remaining >= 0x1000) ? pmm::block_size : remaining;

                    memcpy(read_data + i, (void*)(pa + phys_mem_map), transfer);
                }

                pmm::free_block(pa);
            }

            // First check if we are adjacent to any other regions
            for(auto& region : _regions) {
                // Underneath it
                if((obj.base + aligned_len) == region.base) {
                    region.base -= aligned_len;
                    region.len += aligned_len;

                    return;
                }

                // Above it
                if((region.base + region.len) == obj.base) {
                    region.len += aligned_len;
                    return;
                }
            }

            // Nope, just push our own entry, might create some adjacent regions that could be merged but its no issue
            _regions.push_back({obj.base, aligned_len});
        }

        private:
        pci::Device* _device;
        std::vector<Region> _regions;
    };
} // namespace iovmm
