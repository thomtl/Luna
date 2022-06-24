#pragma once

#include <Luna/common.hpp>
#include <Luna/drivers/gpu/gpu.hpp>

#include <Luna/misc/stivale2.hpp>

namespace lfb {
    class Gpu final : public gpu::AbstractGpu {
        public:
        Gpu(stivale2::Parser& boot);

        const std::span<gpu::Mode> get_modes();
        bool set_mode(const gpu::Mode& mode);
        uint8_t* get_lfb() const;

        private:
        stivale2_struct_tag_framebuffer* tag;
        gpu::Mode mode;
    };

    void init(stivale2::Parser& boot);
} // namespace lfb
