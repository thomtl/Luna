#pragma once

#include <Luna/common.hpp>
#include <Luna/drivers/gpu/gpu.hpp>
#include <Luna/drivers/pci.hpp>

#include <Luna/mm/iovmm.hpp>

extern "C" {
    #include "../../../../../subprojects/lil/src/intel.h"
}

namespace intel_gpu {
    class Gpu final : public gpu::AbstractGpu {
        public:
        Gpu(pci::Device* dev);

        const std::span<gpu::Mode> get_modes();
        bool set_mode(const gpu::Mode& mode);
        uint8_t* get_lfb() const;

        bool is_connected() const {
            return connector != nullptr;
        }

        private:
        LilGpu ctx;
        LilConnector* connector;
        LilConnectorInfo ctx_info;

        pci::Device* dev;
        gpu::Mode modes[4];
        iovmm::Iovmm mm;
        iovmm::Iovmm::Allocation gtt_dummy, lfb;
    };
} // namespace intel_gpu