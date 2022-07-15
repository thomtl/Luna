#include <Luna/drivers/gpu/lfb/lfb.hpp>
#include <Luna/mm/vmm.hpp>
#include <Luna/misc/format.hpp>

lfb::Gpu::Gpu(stivale2::Parser& boot) {
    tag = (stivale2_struct_tag_framebuffer*)boot.get_tag(STIVALE2_STRUCT_TAG_FRAMEBUFFER_ID);

    mode.width = tag->framebuffer_width;
    mode.height = tag->framebuffer_height;
    mode.bpp = tag->framebuffer_bpp;
    mode.pitch = tag->framebuffer_pitch;

    auto pa = tag->framebuffer_addr;
    auto& kvmm = vmm::KernelVmm::get_instance();
    for(size_t i = 0; i < (mode.height * mode.pitch); i += pmm::block_size)
        kvmm.map(pa + i, pa + i + phys_mem_map, paging::mapPagePresent | paging::mapPageWrite, msr::pat::wc);
}

const std::span<gpu::Mode> lfb::Gpu::get_modes() {
    return std::span<gpu::Mode>(&mode, 1);
}

bool lfb::Gpu::set_mode(const gpu::Mode& mode) {
    if(mode != this->mode)
        return false; // LFB driver so we cannot modeset
    
    return true;
}

uint8_t* lfb::Gpu::get_lfb() const {
    return (uint8_t*)(tag->framebuffer_addr + phys_mem_map);
}

void lfb::init(stivale2::Parser& boot) {
    auto* lfb_gpu = new Gpu{boot};

    gpu::get_gpu().register_gpu(lfb_gpu);
    gpu::get_gpu().make_gpu_main(lfb_gpu);
    gpu::get_gpu().set_mode(lfb_gpu->get_modes()[0]);
}