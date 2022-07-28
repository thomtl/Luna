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



intel_gpu::Gpu::Gpu(pci::Device* dev): dev{dev}, mm{dev} {
    dev->set_privileges(pci::privileges::Pio | pci::privileges::Mmio | pci::privileges::Dma);

    mm.push_region({.base = 0x1000, .len = 0xFF'FFFF'FFFF - 0x1000});

    lil_init_gpu(&ctx, dev);

    for(size_t i = 0; i < ctx.num_connectors; i++) {
        auto& connector = ctx.connectors[i];
        if(!connector.is_connected(&ctx, &connector))
            continue;

        ctx_info = connector.get_connector_info(&ctx, &connector);

        for(size_t i = 0; i < 4; i++) {
            auto& mode = modes[i];
            mode.width = ctx_info.modes[i].hactive;
            mode.height = ctx_info.modes[i].vactive;

            mode.bpp = 32;
            mode.pitch = mode.width * (mode.bpp / 8);
            mode.pitch = align_up(mode.pitch, 64);

            //print("Mode: {}x{}x{}\n", mode.width, mode.height, mode.bpp);
        }
    
        this->connector = &connector;
        return;
    }

    // Wasn't able to find connector sadly
}

bool intel_gpu::Gpu::set_mode(const gpu::Mode& mode) {
    if(!connector)
        return false;

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
    
    auto& crtc = *connector->crtc;
    auto& plane = crtc.planes[0];

    crtc.current_mode = ctx_info.modes[i];
    plane.surface_address = 0;
    plane.enabled = true;

    crtc.shutdown(&ctx, &crtc);
    ctx.vmem_clear(&ctx);

    if(gtt_dummy)
        mm.free(gtt_dummy);

    if(lfb)
        mm.free(lfb);
    
    // Setup Dummy GTT
    {
        gtt_dummy = mm.alloc(0x1000, iovmm::Iovmm::Bidirectional, msr::pat::wc);
        
        for(size_t i = 0; i < (ctx.gtt_size / 1024); i++)
            ctx.vmem_map(&ctx, gtt_dummy.guest_base, i * pmm::block_size);
    }

    constexpr size_t gtt_lfb_offset = 0;
    // Setup real LFB
    {
        lfb = mm.alloc(mode.pitch * mode.height, iovmm::Iovmm::Bidirectional, msr::pat::wc);

        for(size_t i = 0; i < lfb.len; i += 0x1000)
            ctx.vmem_map(&ctx, lfb.guest_base + i, gtt_lfb_offset + i);
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



static void init(pci::Device& dev) {
    auto* igpu = new intel_gpu::Gpu{&dev};
    if(!igpu->is_connected())
        return;
    
    gpu::get_gpu().register_gpu(igpu);
    gpu::get_gpu().set_mode_and_make_main(igpu, igpu->get_modes()[0]);
    print("gpu: Using Native Intel GPU driver\n");
}

static std::pair<uint16_t, uint16_t> known_intel_gpus[] = {
    {0x8086, 0x0166}, // Intel 3rd Gen Core GPU (Ivy Bridge)
    {0x8086, 0x5917}, // Intel 8th Gen Core GPU (Coffee Lake)
};

static pci::Driver driver = {
    .name = "Intel i915 GPU Driver",
    .init = init,

    .match = pci::match::vendor_device,
    .id_list = {known_intel_gpus}
};
DECLARE_PCI_DRIVER(driver);