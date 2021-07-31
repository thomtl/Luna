#pragma once

#include <Luna/common.hpp>

#include <std/span.hpp>
#include <std/vector.hpp>
#include <std/string.hpp>

namespace gpu
{
    struct Mode {
        bool operator==(const Mode& other) const {
            return (width == other.width) && (height == other.height) && (pitch == other.pitch) && (bpp == other.bpp);
        }

        bool operator!=(const Mode& other) const {
            return (width != other.width) && (height != other.height) && (pitch != other.pitch) && (bpp != other.bpp);
        }

        size_t width, height, pitch;
        uint8_t bpp;
    };

    class AbstractGpu {
        public:
        virtual ~AbstractGpu() {}

        virtual const std::span<Mode> get_modes() = 0;
        virtual bool set_mode(const Mode& mode) = 0;
        virtual uint8_t* get_lfb() const = 0;
    };

    struct Rect {
        size_t x, y, w, h;
    };

    struct GpuManager {
        void register_gpu(AbstractGpu* gpu);
        void make_gpu_main(AbstractGpu* gpu);
        AbstractGpu* get_main_gpu() { return main_gpu; }

        void set_mode(const gpu::Mode& mode);
        Mode get_mode() const;

        uint8_t* get_fb();
        void clear_backbuffer();
        void flush();
        void flush(const Rect& rect);

        private:
        std::vector<AbstractGpu*> gpus;

        AbstractGpu* main_gpu;
        Mode curr_mode;

        uint8_t* backbuffer;
    };
    GpuManager& get_gpu();
} // namespace gpu
