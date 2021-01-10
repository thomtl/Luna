#include <Luna/drivers/gpu/intel/gpu.hpp>

#include <Luna/misc/format.hpp>

#include <Luna/mm/vmm.hpp>


#include <std/linked_list.hpp>
#include <std/span.hpp>

#include <std/string.hpp>

struct Mode {
    size_t width, height, pitch;
    uint8_t bpp;
};

struct {
    uint16_t vid, did;
} known_intel_gpus[] = {
    {0x8086, 0x0166}, // Intel 3rd Gen Core GPU (Ivy Bridge)
};

intel_gpu::Gpu::Gpu(pci::Device* dev): dev{dev}, mm{dev} {
    dev->set_privileges(pci::privileges::Pio | pci::privileges::Mmio | pci::privileges::Dma);

    mm.push_region({.base = 0x1000, .len = 0xFFFF'FFFF - 0x1000});

    lil_init_gpu(&ctx, dev);

    auto& connector = ctx.connectors[0];
    ctx_info = connector.get_connector_info(&ctx, &connector);

    for(size_t i = 0; i < 4; i++) {
        auto& mode = modes[i];
        mode.width = ctx_info.modes[i].hactive;
        mode.height = ctx_info.modes[i].vactive;

        mode.bpp = 32;
        mode.pitch = mode.width * (mode.bpp / 8);
        mode.pitch = align_up(mode.pitch, 64);
    }

    if(!connector.is_connected(&ctx, &connector))
        PANIC("Connector 0 is not connected");
}

bool intel_gpu::Gpu::set_mode(const gpu::Mode& mode) {
    size_t i = 0;
    bool found = false;
    for(; i < 4; i++) {
        if(modes[i] == mode) {
            found = true;
            break;
        }
    }

    if(!found)
        return false;
    
    auto& connector = ctx.connectors[0];
    auto& crtc = *connector.crtc;
    auto& plane = crtc.planes[0];

    crtc.current_mode = ctx_info.modes[i];
    plane.surface_address = 0;
    plane.enabled = true;

    crtc.shutdown(&ctx, &crtc);
    lil_vmem_clear(&ctx);

    if(gtt_dummy)
        mm.free(gtt_dummy);

    if(lfb)
        mm.free(lfb);
    
    // Setup Dummy GTT
    {
        gtt_dummy = mm.alloc(0x1000, iovmm::Iovmm::Bidirectional, msr::pat::wc);
        
        for(size_t i = 0; i < (ctx.gtt_size / 1024); i++)
            lil_vmem_map(&ctx, gtt_dummy.guest_base, i * pmm::block_size, 0b110);
    }

    constexpr size_t gtt_lfb_offset = 0;
    // Setup real LFB
    {
        lfb = mm.alloc(mode.pitch * mode.height, iovmm::Iovmm::Bidirectional, msr::pat::wc);

        for(size_t i = 0; i < lfb.len; i += 0x1000)
            lil_vmem_map(&ctx, lfb.guest_base + i, gtt_lfb_offset + i, 0b110);
    }

    crtc.commit_modeset(&ctx, &crtc);
    
    plane.update_surface(&ctx, &plane, gtt_lfb_offset, mode.pitch);

    return true;
}

const std::span<gpu::Mode> intel_gpu::Gpu::get_modes() {
    return std::span<gpu::Mode>{modes, 4};
}

uint8_t* intel_gpu::Gpu::get_lfb() const {
    return lfb.host_base;
}

void intel_gpu::init() {
    for(const auto [vid, did] : known_intel_gpus) {
        auto* dev = pci::device_by_id(vid, did, 0);
        if(!dev)
            continue;

        auto* igpu = new Gpu{dev};
        gpu::get_gpu().register_gpu(igpu);
        gpu::get_gpu().make_gpu_main(igpu);
        gpu::get_gpu().set_mode(igpu->get_modes()[0]);
        print("gpu: Using Native Intel GPU driver\n");

        return;
    }
}