#include <Luna/drivers/sound/hda.hpp>

#include <Luna/cpu/tsc.hpp>
#include <Luna/cpu/idt.hpp>
#include <Luna/mm/vmm.hpp>

#include <Luna/fs/vfs.hpp>

#include <Luna/misc/log.hpp>
#include <std/linked_list.hpp>


constexpr const char* widget_type_strs[] = {
    "Audio Output (DAC)",
    "Audio Input (ADC)",
    "Audio Mixer",
    "Audio Selector",
    "Pin Complex",
    "Power Widget",
    "Volume Knob Widget",
    "Beep Generator Widget",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Vendor Defined Widget"
};

constexpr const char* default_device_strs[] = {
    "Line Out",
    "Speaker",
    "HP Out",
    "CD",
    "SPDIF Out",
    "Digital Other Out",
    "Modem Line Side",
    "Modem Handset Side",
    "Line In",
    "AUX",
    "Mic In",
    "Telephony",
    "SPDIF In",
    "Digital Other In",
    "Reserved",
    "Other"
};

struct {
    uint32_t frq;
    uint8_t base, mult, div;
} hda_formats[] = {
    {.frq = 8000, .base = 48, .mult = 1, .div = 6},
    {.frq = 9600, .base = 48, .mult = 1, .div = 5},
    {.frq = 11025, .base = 44, .mult = 1, .div = 4},
    {.frq = 16000, .base = 48, .mult = 1, .div = 3},
    {.frq = 22050, .base = 44, .mult = 1, .div = 2},
    {.frq = 32000, .base = 48, .mult = 2, .div = 3},
    {.frq = 44100, .base = 44, .mult = 1, .div = 1},
    {.frq = 48000, .base = 48, .mult = 1, .div = 1},
    {.frq = 88200, .base = 44, .mult = 2, .div = 1},
    {.frq = 96000, .base = 48, .mult = 2, .div = 1},
    {.frq = 176400, .base = 44, .mult = 4, .div = 1},
    {.frq = 192000, .base = 48, .mult = 4, .div = 1},
};


hda::HDAController::HDAController(pci::Device& device, uint16_t vendor, uint32_t quirks): quirks{quirks}, mm{&device} {
    device.set_privileges(pci::privileges::Mmio | pci::privileges::Dma);

    auto bar = device.read_bar(0);
    ASSERT(bar.type == pci::Bar::Type::Mmio);

    auto vector = idt::allocate_vector();
    device.enable_irq(0, vector);
    idt::set_handler(vector, idt::Handler{.f = [](uint8_t, idt::Regs*, void* userptr){
        auto& self = *(HDAController*)userptr;
        self.handle_irq();
    }, .is_irq = true, .should_iret = true, .userptr = this});

    vmm::get_kernel_context().map(bar.base, bar.base + phys_mem_map, paging::mapPagePresent | paging::mapPageWrite, msr::pat::uc);

    regs = (volatile Regs*)(bar.base + phys_mem_map);
    ssync = (quirks & quirkOldSsync) ? &regs->old_ssync : &regs->ssync;

    uint16_t major = regs->vmaj, minor = regs->vmin;
    print("hda: Controller {}.{}\n", major, minor);

    (void)vendor;
    if(quirks & hda::quirkTCSEL) {
        constexpr uint16_t tcsel = 0x44;
        auto v = device.read<uint8_t>(tcsel);
        v &= ~0b111;
        device.write<uint8_t>(tcsel, v);

        print("     TCSEL: Cleared\n");
    }

    if(quirks & hda::quirkSnoopATI) {
        constexpr uint16_t cntr2_addr = 0x42;
        constexpr uint32_t enable_snoop = 0x2;

        auto v = device.read<uint16_t>(cntr2_addr);
        v &= ~0b111;
        v |= (quirks & hda::quirkNoSnoop) ? 0 : enable_snoop;
        device.write<uint16_t>(cntr2_addr, v);
        print("     ATI Snoop: {}\n", (quirks & hda::quirkNoSnoop) ? "Disabled" : "Enabled");
    }

    if(quirks & hda::quirkSnoopSCH) { // Enable PCH/SCH snoop if needed
        constexpr uint16_t sch_devc = 0x78;
        constexpr uint32_t devc_nosnoop = (1 << 11);

        auto snoop = device.read<uint16_t>(sch_devc);
        if(((quirks & hda::quirkNoSnoop) && !(snoop & devc_nosnoop)) || (!(quirks & hda::quirkNoSnoop) && (snoop & devc_nosnoop))) {
            snoop &= ~devc_nosnoop;
            if(quirks & hda::quirkNoSnoop)
                snoop |= devc_nosnoop;
            device.write<uint16_t>(sch_devc, snoop);
            snoop = device.read<uint16_t>(sch_devc);
        }
        print("     SCH Snoop: {}\n", (snoop & devc_nosnoop) ? "Disabled" : "Enabled");
    }

    if((regs->gctl & 1) == 0) {
        // Bring controller out of reset

        regs->gctl |= 1;
        while((regs->gctl & 1) == 0)
            asm("pause");
    }

    a64 = regs->gcap & 1;
    if(a64)
        mm.push_region({0x1000, 0xFFFF'FFFF'FFFF'FFFF - 0x1000});
    else
        mm.push_region({0x1000, 0xFFFF'FFFF - 0x1000});

    // Only really need to wait 521us for codecs to wake up, but give it a little bit of time
    tsc::poll_sleep(10_ms);

    oss = (regs->gcap >> 12) & 0xF;
    iss = (regs->gcap >> 8) & 0xF;
    bss = (regs->gcap >> 3) & 0x1F;
    print("     OSS: {}, ISS: {}, BSS: {}\n", oss, iss, bss);

    stream_ids = std::bitmap{16}; // 15 StreamIDs to allocate, excluding 0
    stream_ids.set(0); // StreamID 0 is reserved

    oss_bitmap = std::bitmap{oss};
    iss_bitmap = std::bitmap{iss};
    bss_bitmap = std::bitmap{bss};

    in_descriptors = (volatile StreamDescriptor*)(bar.base + phys_mem_map + sizeof(Regs));
    out_descriptors = (volatile StreamDescriptor*)(bar.base + phys_mem_map + sizeof(Regs) + (iss * sizeof(StreamDescriptor)));
    bi_descriptors = (volatile StreamDescriptor*)(bar.base + phys_mem_map + sizeof(Regs) + (iss * sizeof(StreamDescriptor)) + (oss * sizeof(StreamDescriptor)));

    regs->wakeen = 0; // Allow no Codecs to generate Wake events

    *ssync = 0x3FFF'FFFF;

    regs->rirbsts = (1 << 0) | (1 << 2);

    auto calculate_ringbuffer_size = [&](uint8_t size) -> std::pair<uint8_t, size_t> {
        auto mask = (size >> 4) & 0xF;
        size_t index = 0;
        for(size_t i = 0; i < 3; i++)
            if(mask & (1 << i))
                index = i;

        if(index == 0) return {index, 2};
        else if(index == 1) return {index, 16};
        else if(index == 2) return {index, 256};
        else PANIC("Unknown size");
    };

    // Init CORB
    if(regs->corbctl & (1 << 1)) 
        regs->corbctl &= ~(1 << 1); // Stop CORB DMA Engine

    const auto [corbsize_i, corbsize] = calculate_ringbuffer_size(regs->corbsize);
    corb.size = corbsize;

    print("   - CORB: {} Entries ... ", corb.size);

    corb.alloc = mm.alloc(corb.size * sizeof(uint32_t), iovmm::Iovmm::HostToDevice);
    corb.corb = (volatile uint32_t*)corb.alloc.host_base;
    regs->corblbase = corb.alloc.guest_base & 0xFFFF'FFFF;
    if(a64)
        regs->corbubase = (corb.alloc.guest_base >> 32) & 0xFFFF'FFFF;

    uint64_t tmp = regs->corbsize;
    tmp &= ~0b11;
    tmp |= corbsize_i;
    regs->corbsize = tmp;

    regs->corbrp |= (1 << 15);
    while(!(regs->corbrp & (1 << 15)))
        asm("pause");
    regs->corbrp &= ~(1 << 15);
    while(regs->corbrp & (1 << 15))
        asm("pause");


    regs->corbwp &= ~0xFF; // Reset CORB Write pointer

    regs->corbctl |= (1 << 1); // Start CORB engine

    print("Started\n");
    if(regs->rirbctl & (1 << 1)) 
        regs->rirbctl &= ~(1 << 1); // Stop RIRB DMA Engine

    const auto [rirbsize_i, rirbsize] = calculate_ringbuffer_size(regs->rirbsize);
    rirb.size = rirbsize;
    rirb.head = 0;

    print("   - RIRB: {} Entries ... ", rirb.size);

    rirb.alloc = mm.alloc(rirb.size * sizeof(uint32_t) * 2, iovmm::Iovmm::DeviceToHost);
    rirb.rirb = (volatile uint32_t*)rirb.alloc.host_base;
    regs->rirblbase = rirb.alloc.guest_base & 0xFFFF'FFFF;
    if(a64)
        regs->rirbubase = (rirb.alloc.guest_base >> 32) & 0xFFFF'FFFF;

    tmp = regs->rirbsize;
    tmp &= ~0b11;
    tmp |= rirbsize_i;
    regs->rirbsize = tmp;

    regs->rirbwp |= (1 << 15); // Clear RIRB Write Pointer
    regs->rirbcnt = 1;
    regs->rirbctl |= (1 << 1) | (1 << 0); // Start RIRB engine

    print("Started\n");

    regs->gctl |= (1 << 8); // Accept unsolicited responses

    regs->intctl = (1u << 31) | (1 << 30); // Global IRQ enable + Controller IRQ enable


    auto detected_codecs = regs->statests;
    for(size_t i = 0; i < 15; i++)
        if(detected_codecs & (1 << i))
            enumerate_codec(i);
}

hda::HDAController::~HDAController() {
    mm.free(corb.alloc);
    mm.free(rirb.alloc);
}

uint32_t hda::HDAController::corb_cmd(uint8_t codec, uint8_t node, uint32_t cmd) {
    auto enqueue = [&](uint32_t verb) {
        auto index = (regs->corbwp + 1) % corb.size;
        ASSERT(index != regs->corbrp);

        corb.corb[index] = verb;

        regs->corbwp = index;
    };

    uint32_t verb = 0;
    verb |= (codec & 0xF) << 28;
    verb |= node << 20;
    verb |= cmd & 0xF'FFFF;

    codecs[codec].have_response = false;

    enqueue(verb);

    while(!codecs[codec].have_response)
        asm("pause");

    return codecs[codec].curr_response;
}


#define VERB(c) static_cast<uint16_t>(c)
void hda::HDAController::verb_set_pin_widget_control(const Widget& node, uint8_t v) {
    ASSERT(corb_cmd(node.codec_id, node.nid, (VERB(Verb::SetPinWidgetControl) << 8) | v) == 0);
}

void hda::HDAController::verb_set_eapd_enable(const Widget& node, uint8_t v) {
    ASSERT(corb_cmd(node.codec_id, node.nid, (VERB(Verb::SetEAPDEnable) << 8) | v) == 0);
}

void hda::HDAController::verb_set_amp_gain_mute(const Widget& node, uint16_t v) {
    ASSERT(corb_cmd(node.codec_id, node.nid, (VERB(Verb::SetAmpGainMute) << 16) | v) == 0);
}

void hda::HDAController::verb_set_processing_state(const Widget& node, ProcessingMode mode) {
    ASSERT(corb_cmd(node.codec_id, node.nid, (VERB(Verb::SetProcessingState) << 8) | static_cast<uint8_t>(mode)) == 0);
}

void hda::HDAController::verb_set_connection_select(const Widget& node, uint8_t connection) {
    ASSERT(corb_cmd(node.codec_id, node.nid, (VERB(Verb::SetConnectionSelect) << 8) | connection) == 0);
}

void hda::HDAController::verb_set_coefficient(const Widget& node, uint16_t coefficient, uint16_t value) {
    ASSERT(corb_cmd(node.codec_id, node.nid, (VERB(Verb::SetCoefficientIndex) << 16) | coefficient) == 0);
    ASSERT(corb_cmd(node.codec_id, node.nid, (VERB(Verb::SetProcessingCoefficient) << 16) | value) == 0);
}

uint16_t hda::HDAController::verb_get_coefficient(const Widget& node, uint16_t coefficient) {
    ASSERT(corb_cmd(node.codec_id, node.nid, (VERB(Verb::SetCoefficientIndex) << 16) | coefficient) == 0);
    return corb_cmd(node.codec_id, node.nid, (VERB(Verb::GetProcessingCoefficient) << 16));
}

void hda::HDAController::verb_set_power_state(uint8_t codec, uint8_t node, uint8_t state) {
    ASSERT(corb_cmd(codec, node, (VERB(Verb::SetPowerState) << 8) | (state & 0xF)) == 0);
}

void hda::HDAController::verb_set_power_state(const Widget& node, uint8_t state) {
    verb_set_power_state(node.codec_id, node.nid, state);
}

uint32_t hda::HDAController::verb_get_power_state(const Widget& node) {
    return corb_cmd(node.codec_id, node.nid, (VERB(Verb::GetPowerState) << 8));
}

uint32_t hda::HDAController::verb_get_parameter(uint8_t codec, uint8_t node, WidgetParameter param) {
    return corb_cmd(codec, node, (VERB(Verb::GetParameter) << 8) | static_cast<uint16_t>(param));
}

uint32_t hda::HDAController::verb_get_parameter(const Widget& node, WidgetParameter param) {
    return verb_get_parameter(node.codec_id, node.nid, param);
}

uint32_t hda::HDAController::verb_get_config_default(const Widget& node) {
    return corb_cmd(node.codec_id, node.nid, (VERB(Verb::GetConfigDefault) << 8));
}

void hda::HDAController::verb_set_stream_format(const Widget& node, uint16_t v) {
    ASSERT(corb_cmd(node.codec_id, node.nid, (VERB(Verb::SetStreamFormat) << 16) | v) == 0);
}

void hda::HDAController::verb_set_converter_stream_channel(const Widget& node, uint8_t stream, uint8_t channel) {
    ASSERT(corb_cmd(node.codec_id, node.nid, (VERB(Verb::SetChannelStreamID) << 8) | (stream << 4) | (channel)) == 0);
}
#undef VERB

void hda::HDAController::enumerate_codec(uint8_t index) {
    print("   - Codec {}:\n", (uint16_t)index);
    auto& codec = codecs[index];

    auto res = verb_get_parameter(index, 0, WidgetParameter::VendorID);
    codec.vid = (res >> 16) & 0xFFFF;
    codec.did = res & 0xFFFF;

    res = verb_get_parameter(index, 0, WidgetParameter::RevisionID);
    codec.version_major = (res >> 20) & 0xF;
    codec.verion_minor = (res >> 16) & 0xF;

    res = verb_get_parameter(index, 0, WidgetParameter::SubordinateNodeCount);
    codec.starting_node = (res >> 16) & 0xFF;
    codec.n_nodes = res & 0xFF;

    print("     VendorID: {:#x}, DeviceID: {:#x}\n", codec.vid, codec.did);
    print("     Revision: {}.{}\n", codec.version_major, codec.verion_minor);
    print("     Subordinate Nodes: {} -> {}\n", codec.starting_node, codec.starting_node + codec.n_nodes);

    for(uint8_t func_group_node = codec.starting_node; func_group_node < (codec.starting_node + codec.n_nodes); func_group_node++) {
        res = verb_get_parameter(index, func_group_node, WidgetParameter::FunctionGroupType);
        res &= ~0x100; // Clear Unsolicited capable bit

        if(res == 1) {
            print("     - Node {}: Audio Function Group\n", (uint16_t)func_group_node);

            verb_set_power_state(index, func_group_node, 0); // Wake up
            tsc::poll_sleep(200_ms);

            res = verb_get_parameter(index, func_group_node, WidgetParameter::SubordinateNodeCount);
            auto start = (res >> 16) & 0xFF;
            auto len = res & 0xFF;

            print("       Subordinate Nodes: {} -> {}\n", start, start + len);

            for(uint8_t i = start; i < (start + len); i++)
                enumerate_widget(index, i, func_group_node);
        } else {
            print("     - Node {}: Unknown Function Group {:#x}\n", (uint16_t)func_group_node, res);
        }
    }

    for(auto& [_, widget] : codec.widgets) {
        if(widget.type != WidgetType::PinComplex) // Ignore all non Pin Complexes
            continue;

        Path path{};
        path.codec = index;
        path.type = (widget.config_default >> 20) & 0xF;

        auto connectivity = (widget.config_default >> 30) & 0b11;

        if(connectivity == 0b00) path.loc = Path::Location::External;
        else if(connectivity == 0b10) path.loc = Path::Location::Internal;
        else if(connectivity == 0b11) path.loc = Path::Location::Both;
        //else if(connectivity == 0b01) continue; // Nothing attached(/)

        constexpr size_t max_depth = 10; // TODO: Is this a good pick?

        size_t depth = 0;
        auto solve = [&](uint8_t nid) {
            auto solve_impl = [&](auto& self, uint8_t nid) -> void {
                path.path.push_back(nid);
                
                if(depth++ > max_depth)
                    return;
                
                auto& curr = codec.widgets[nid];
                switch(curr.type) {
                    using enum WidgetType;
                    case DAC: // DAC, Should have no further connections, begin return chain
                        path.nid_dac = nid;

                        ASSERT(curr.connection_list.size() == 0);
                        break;
                    
                    case Mixer: // Mixer, "Sums" N signals into 1, for now only support 1st node
                        path.nid_mixer = nid;

                        ASSERT(curr.connection_list.size() >= 1);
                        if(path.nid_mixer.has_value())
                            print("hda: TODO: Support multiple mixers\n");
                        
                        self(self, curr.connection_list[0]);
                        break;

                    case PinComplex: // Pin Complex
                        path.nid_pin = nid;
                        
                        self(self, curr.connection_list[0]); // Select the first connection, we'll select that one later on in stream_create
                        break;

                    default:
                        print("hda: Unknown {} in path\n", widget_type_strs[static_cast<uint8_t>(curr.type)]);
                        break;
                }
            };

            if(widget.connection_list.size() >= 1) // Audio outputs will have 1 connection at least
                solve_impl(solve_impl, nid);
        };

        solve(widget.nid); // Try to parse path for every node
            
        if(!path.nid_dac.has_value())
            continue; // Couldn't find a DAC

        codec.paths.push_back(path);
    }

    for(auto& path : codec.paths) {
        print("     Device: {} - {}\n", default_device_strs[path.type], Path::location_to_str(path.loc));
    }
}

void hda::HDAController::enumerate_widget(uint8_t codec, uint8_t node, uint8_t function_group) {
    auto& widget = codecs[codec].widgets[node];
    widget.codec_id = codec;
    widget.nid = node;

    auto res = verb_get_parameter(widget, WidgetParameter::AudioWidgetCap);

    auto type = (res >> 20) & 0xF;
    print("       - Node {}: {} ", (uint16_t)node, widget_type_strs[type]);

    widget.type = static_cast<WidgetType>(type);
    widget.function_group = function_group;

    widget.cap = res;
    widget.pin_cap = verb_get_parameter(widget, WidgetParameter::PinCap);

    if(!(widget.cap & (1 << 3))) {
        widget.in_amp_cap = verb_get_parameter(codec, function_group, WidgetParameter::InAmpCap);
        widget.out_amp_cap = verb_get_parameter(codec, function_group, WidgetParameter::OutAmpCap);
    } else {
        widget.in_amp_cap = verb_get_parameter(widget, WidgetParameter::InAmpCap);
        widget.out_amp_cap = verb_get_parameter(widget, WidgetParameter::OutAmpCap);
    }

    widget.processing_cap = verb_get_parameter(widget, WidgetParameter::ProcessingCap);
    widget.supported_power_states = verb_get_parameter(widget, WidgetParameter::SupportedPowerStates);
    
    widget.volume_knob_cap = verb_get_parameter(widget, WidgetParameter::VolumeKnobCap);

    widget.config_default = verb_get_config_default(widget);

    if(widget.cap & (1 << 8)) { // Connection list present
        res = verb_get_parameter(widget, WidgetParameter::ConnectionListLength);
        auto long_form = (res >> 7) & 1;
        uint8_t len = res & 0x7F;

        auto push = [&](uint16_t v) {
            if(!v) // Connection list entries are only valid if they're non-null
                return;
            
            widget.connection_list.push_back(v);
        };

        if(long_form) {
            print("hda: Long-form Connection List Detected, TODO: Handle NIDs > 7 bits\n");
            for(uint8_t i = 0; i < len; i += 2) {
                res = corb_cmd(codec, node, (0xF02 << 8) | i);

                push(res & 0xFFFF);
                push((res >> 16) & 0xFFFF);
            }
        } else {
            for(uint8_t i = 0; i < len; i += 4) {
                res = corb_cmd(codec, node, (0xF02 << 8) | i);

                push(res & 0xFF);
                push((res >> 8) & 0xFF);
                push((res >> 16) & 0xFF);
                push((res >> 24) & 0xFF);
            }
        }
        
        print("Connection List: ");
        for(auto entry : widget.connection_list) 
            print("{} ", entry);
    }
    print("\n");
}

void hda::HDAController::handle_irq() {
    constexpr uint8_t mask = (1 << 0) | (1 << 2);

    if(regs->rirbsts & mask) {
        regs->rirbsts = mask; // Clear RIRB Overrun status if needed

        auto wp = regs->rirbwp;
        while(rirb.head != wp) {
            rirb.head = (rirb.head + 1) % rirb.size;

            uint64_t res = rirb.rirb[2 * rirb.head];
            uint64_t ext = rirb.rirb[2 * rirb.head + 1];

            if(ext & (1 << 4))
                print("hda: TODO: Handle Unsolicited responses\n");
            else {
                uint8_t codec_i = ext & 0xF;

                codecs[codec_i].curr_response = res;
                codecs[codec_i].have_response = true;
            }
        }
    }
}

std::optional<hda::Stream> hda::HDAController::stream_create(const hda::StreamParameters& params, size_t entries, Path& device) {
    Stream stream{};
    stream.params = params;
    stream.device = &device;

    auto stream_id = stream_ids.get_free_bit();
    if(stream_id == ~0ull)
        return std::nullopt; // Was not able to allocate StreamID
    stream_ids.set(stream_id);

    stream.stream_id = stream_id;

    auto reset_stream = [&]() {
        stream.desc->ctl &= ~(1 << 1); // Clear DMA Run

        stream.desc->ctl |= 1; // Enter Reset
        tsc::poll_sleep(3_us);

        uint64_t timeout = 300;
        while(!(stream.desc->ctl & 1) && timeout-- >= 1) // Wait Device to enter reset state
            asm("pause");

        stream.desc->ctl &= ~1; // Leave Reset mode
        tsc::poll_sleep(3_us);

        timeout = 300;
        while((stream.desc->ctl & 1) && timeout-- >= 1) // Wait Device to enter reset state
            asm("pause");
    };

    // If we can't find a normal In Descriptor, or Out Descriptor, try a Bidirectional descriptor
    auto try_bidir = [&]() -> bool {
        auto bss_id = bss_bitmap.get_free_bit();
        if(bss_id == ~0ull)
            return false; // Was not able to allocate bssID
        bss_bitmap.set(bss_id);

        stream.desc = &bi_descriptors[bss_id];

        reset_stream();
        stream.desc->ctl_high |= (params.dir << 3); // Program Stream Direction, this needs to be done immediately after reset, before anything else, because it fundamentally changes the stream
        return true;
    };

    if(params.dir) { // Write
        auto oss_id = oss_bitmap.get_free_bit();
        if(oss_id == ~0ull) {
            if(!try_bidir())
                return std::nullopt;
        } else {
            oss_bitmap.set(oss_id);

            stream.desc = &out_descriptors[oss_id];
            reset_stream();
        }
    } else {
        auto iss_id = iss_bitmap.get_free_bit();
        if(iss_id == ~0ull) {
            if(!try_bidir())
                return std::nullopt;
        } else {
            iss_bitmap.set(iss_id);

            stream.desc = &in_descriptors[iss_id];
            reset_stream();
        }
    }

    stream.stream_descriptor_index = (stream.desc - in_descriptors) / sizeof(StreamDescriptor);

    stream.desc->ctl_high |= (stream_id << 4) | (1 << 2); // Set StreamID, Attempt to treat as Preffered Trafic

    stream.fmt = 0; // Bit 15 is 0 => PCM Data

    bool found = false;
    uint8_t base, mult, div;
    for(const auto& ent : hda_formats) {
        if(params.sample_rate == ent.frq) {
            base = (ent.base == 48) ? 0 : 1;
            mult = ent.mult;
            div = ent.div;
            found = true;
        }
    }
    if(!found)
        return std::nullopt; // Unknown sample size

    stream.fmt |= ((base & 1) << 14);
    stream.fmt |= (((mult - 1) & 0x7) << 11);
    stream.fmt |= (((div - 1) & 0x7) << 8);

    ASSERT(params.sample_size == 16 || params.sample_size == 32); // TODO: Support other container sizes, requires left justifying samples in container
    uint8_t bits_per_sample = 0;
    if(params.sample_size == 8) bits_per_sample = 0b000;
    else if(params.sample_size == 16) bits_per_sample = 0b001;
    else if(params.sample_size == 20) bits_per_sample = 0b010;
    else if(params.sample_size == 24) bits_per_sample = 0b011;
    else if(params.sample_size == 32) bits_per_sample = 0b100;
    else PANIC("Unknown sample size");

    stream.fmt |= ((bits_per_sample & 0x7) << 4);
    stream.fmt |= ((params.channels - 1) & 0xF);

    stream.desc->fmt = stream.fmt;

    stream.entries = entries;
    stream.entries_i = 0;
    entries = min(entries, 2); // HDA spec requires at least 2 entries

    stream.bdl_alloc = mm.alloc(align_up(entries * sizeof(BufferDescriptor), 128), iovmm::Iovmm::HostToDevice);

    stream.desc->bdpl = stream.bdl_alloc.guest_base & 0xFFFF'FFFF;
    if(a64)
        stream.desc->bdpu = (stream.bdl_alloc.guest_base >> 32) & 0xFFFF'FFFF;
        
    stream.bdl = (volatile BufferDescriptorList*)stream.bdl_alloc.host_base;
    stream.desc->lvi = entries - 1;
    stream.desc->cbl = 0;

    auto& codec = codecs[device.codec];

    {
        auto dac_nid = device.nid_dac.value();
        auto& dac = codec.widgets[dac_nid];

        if(dac.cap & (1 << 10)) {
            verb_set_power_state(dac, 0); // Wake up
            while(((verb_get_power_state(dac) >> 4) & 0xF) != 0)
                tsc::poll_sleep(100_ms);
        }

        verb_set_stream_format(dac, stream.fmt);
        verb_set_converter_stream_channel(dac, stream.stream_id, 0);

        if(dac.cap & (1 << 6))
            verb_set_processing_state(dac, ProcessingMode::Benign);

        if(dac.cap & (1 << 2)) { // Out Amp Present
            auto amp_cap = dac.out_amp_cap;

            auto max_gain = (amp_cap >> 8) & 0x7F;
            max_gain /= 4; // Half volume
            max_gain *= 3;
            verb_set_amp_gain_mute(dac, (1 << 15) | (1 << 13) | (1 << 12) | max_gain);
        }
    }

    {
        if(device.nid_mixer.has_value()) {
            auto& mixer = codec.widgets[*device.nid_mixer];
            if(mixer.cap & (1 << 10)) {
                verb_set_power_state(mixer, 0); // Wake up
                while(((verb_get_power_state(mixer) >> 4) & 0xF) != 0)
                    tsc::poll_sleep(100_ms);
            }

            verb_set_connection_select(mixer, 0);

            if(mixer.cap & (1 << 2)) { // Out Amp Present
                auto amp_cap = mixer.out_amp_cap;

                auto max_gain = (amp_cap >> 8) & 0x7F;
                verb_set_amp_gain_mute(mixer, (1 << 15) | (1 << 13) | (1 << 12) | max_gain);
            }
        }
    }
    
    {
        auto pin_nid = device.nid_pin.value();
        auto& pin = codec.widgets[pin_nid];

        if(pin.cap & (1 << 10)) {
            verb_set_power_state(pin, 0); // Wake up
            while(((verb_get_power_state(pin) >> 4) & 0xF) != 0)
                tsc::poll_sleep(100_ms);
        }

        if(!(pin.pin_cap & (1 << 4)))
            print("hda: Warning: Pin is not Output Capable\n");

        if(pin.connection_list.size() > 1)
            verb_set_connection_select(pin, 0);
        
        verb_set_pin_widget_control(pin, (1 << 7) | (1 << 6)); // Enable HighPower, Pin Out
        if(pin.pin_cap & (1 << 16)) // EAPD Capable
            verb_set_eapd_enable(pin, (1 << 1)); // Set EAPD

        if(pin.cap & (1 << 6))
            verb_set_processing_state(pin, ProcessingMode::Benign);
        
        if(pin.cap & (1 << 2)) { // Out Amp Present
            auto amp_cap = pin.out_amp_cap;
            
            auto max_gain = (amp_cap >> 8) & 0x7F;
            verb_set_amp_gain_mute(pin, (1 << 15) | (1 << 13) | (1 << 12) | max_gain);
        }
    }

    return stream;
}

bool hda::HDAController::stream_push(hda::Stream& stream, size_t size, uint8_t* pcm) {
    if(stream.entries_i == stream.entries)
        return false; // Stream is full
    
    ASSERT(stream.params.dir); // Assert its an Out Descriptor
    auto alloc = mm.alloc(size, iovmm::Iovmm::HostToDevice);
    memcpy(alloc.host_base, pcm, size);

    stream.bdl->desc[stream.entries_i].addr = alloc.guest_base;
    stream.bdl->desc[stream.entries_i].len = size;

    stream.total_size += size;
    stream.entries_i++;

    return true;
}

bool hda::HDAController::streams_start(size_t n_streams, hda::Stream** streams) {
    uint32_t stream_mask = 0;
    for(size_t i = 0; i < n_streams; i++) {
        streams[i]->desc->cbl = streams[i]->total_size;

        stream_mask |= (1 << streams[i]->stream_descriptor_index);
    }

    *ssync |= stream_mask;

    for(size_t i = 0; i < n_streams; i++) {
        streams[i]->desc->ctl |= (1 << 1); // Start stream DMA Engine

        while(!(streams[i]->desc->sts & (1 << 5))) // Wait for FIFORDY to be set
            asm("pause");
    }

    *ssync &= ~stream_mask;

    return true;
}

hda::Path* hda::HDAController::get_device(uint8_t codec, uint8_t path) {
    if(codec >= 16 || path >= codecs[codec].paths.size())
        return nullptr;

    return &codecs[codec].paths[path];
}

struct {
    uint16_t vid, did;
    uint32_t quirks;
} hda_quirks[] = {
    {0x8086, 0x2668, hda::quirkTCSEL | hda::quirkOldSsync},
    {0x8086, 0x293E, hda::quirkTCSEL | hda::quirkOldSsync},
    {0x8086, 0x1E20, hda::quirkTCSEL | hda::quirkSnoopSCH},
    {0x8086, 0x9D71, hda::quirkTCSEL | hda::quirkSnoopSCH},
    {0x8086, 0x9C20, hda::quirkTCSEL | hda::quirkSnoopSCH},

    {0x1022, 0x1457, hda::quirkSnoopATI},
    {0x1022, 0x15E3, hda::quirkSnoopATI},
};

static constinit std::linked_list<hda::HDAController> controllers;

static void init(pci::Device& dev) {
    auto vid = dev.read<uint16_t>(0);
    auto did = dev.read<uint16_t>(2);
    uint32_t quirks = 0;
    for(auto& quirk : hda_quirks) {
        if(quirk.vid == vid && quirk.did == did) {
            quirks = quirk.quirks;
            break;
        }
    }

    controllers.emplace_back(dev, vid, quirks);
}

/*void hda::hda_test() {
    //ffmpeg -i input.mp3 -f s16le -acodec pcm_s16le output.pcm
    auto& path = *controllers[0].get_device(0, 0);

    auto stream = controllers[0].stream_create({.dir = true, .sample_rate = 44100, .sample_size = 16, .channels = 2}, 1, path).value();

    auto* file = vfs::get_vfs().open("A:/music.pcm");
    auto size = file->get_size();

    uint8_t* data = new uint8_t[size];
    ASSERT(file->read(0, size, data) == size);

    ASSERT(controllers[0].stream_push(stream, size, data));

    hda::Stream* streams[] = {
        &stream
    };
    ASSERT(controllers[0].streams_start(1, streams));

    delete[] data;
    file->close();
}*/

static std::pair<uint16_t, uint16_t> known_cards[] = {
    {0x8086, 0x2668}, // Intel ICH6 HDA
    {0x8086, 0x293E}, // Intel ICH9 HDA
    {0x8086, 0x1E20}, // Intel 7 Series HDA (Ivy Bridge Mobile)

    {0x8086, 0x9C20}, // Intel 8 Series HDA
    //{0x8086, 0x0A0C}, // Intel Haswell-ULT HDA Controller

    {0x8086, 0x9D71}, // Intel SunrisePoint LP HDA

    {0x1022, 0x1457}, // AMD Family 17h (Models 00h-0fh) HDA Controller
    {0x1022, 0x15E3}, // AMD Family 17h (Models 10h-1fh) HDA Controller

};

static pci::Driver driver = {
    .name = "HDA Sound Driver",
    .init = init,

    .match = pci::match::vendor_device,
    .id_list = {known_cards}
};
DECLARE_PCI_DRIVER(driver);