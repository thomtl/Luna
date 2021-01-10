#pragma once

#include <Luna/common.hpp>
#include <Luna/drivers/gpu/gpu.hpp>
#include <Luna/drivers/pci.hpp>

#include <Luna/mm/iovmm.hpp>

extern "C" {
    #include "../../../../../subprojects/lil/src/intel.h"
    #include "../../../../../subprojects/lil/src/gtt.h"
}

namespace intel_gpu {
    class Gpu : public gpu::AbstractGpu {
        public:
        Gpu(pci::Device* dev);

        const std::span<gpu::Mode> get_modes();
        bool set_mode(const gpu::Mode& mode);
        uint8_t* get_lfb() const;

        private:
        LilGpu ctx;
        LilConnectorInfo ctx_info;

        pci::Device* dev;
        gpu::Mode modes[4];
        iovmm::Iovmm mm;
        iovmm::Iovmm::Allocation gtt_dummy, lfb;
    };

    void init();
} // namespace intel_gpu