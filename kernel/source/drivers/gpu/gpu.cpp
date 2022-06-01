#include <Luna/drivers/gpu/gpu.hpp>
#include <std/string.hpp>

static gpu::GpuManager manager;

gpu::GpuManager& gpu::get_gpu() {
    return manager;
}

void gpu::GpuManager::register_gpu(gpu::AbstractGpu* gpu) {
    gpus.push_back(gpu);
}

void gpu::GpuManager::make_gpu_main(gpu::AbstractGpu* gpu) {
    main_gpu = gpu;
}

void gpu::GpuManager::set_mode(const gpu::Mode& mode) {
    main_gpu->set_mode(mode);
   
    // If modes are compatible, which they likely are when switching from VBE LFB to Native, we don't have to reallocate the backbuffer and trash its contents
    if(mode != curr_mode) {
        if(backbuffer)
            delete[] backbuffer;

        backbuffer = new uint8_t[mode.height * mode.pitch];
        memset(backbuffer, 0, mode.height * mode.pitch);
    }

    curr_mode = mode;
    flush(); // Flush backbuffer to main FB
}

gpu::Mode gpu::GpuManager::get_mode() const {
    return curr_mode;
}

std::span<uint8_t> gpu::GpuManager::get_fb() {
    return std::span<uint8_t>{backbuffer, curr_mode.height * curr_mode.pitch};
}

void gpu::GpuManager::clear_backbuffer() {
    memset(backbuffer, 0, curr_mode.height * curr_mode.pitch);
}

void gpu::GpuManager::flush() {
    memcpy(main_gpu->get_lfb(), backbuffer, curr_mode.height * curr_mode.pitch);
}

void gpu::GpuManager::flush(const gpu::Rect& rect) {
    auto* front = main_gpu->get_lfb();
    auto* back = backbuffer;

    auto off = rect.y * curr_mode.pitch + rect.x;
    front += off; back += off;

    auto size = rect.h * curr_mode.pitch + rect.w;
    memcpy(front, back, size);
}