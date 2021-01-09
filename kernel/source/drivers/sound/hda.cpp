#include <Luna/drivers/sound/hda.hpp>

#include <Luna/drivers/hpet.hpp>
#include <Luna/cpu/idt.hpp>
#include <Luna/mm/vmm.hpp>

#include <Luna/fs/vfs.hpp>

#include <Luna/misc/log.hpp>
#include <std/linked_list.hpp>


constexpr const char* widget_type_strs[] = {
    "Audio Output",
    "Audio Input",
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


hda::HDAController::HDAController(pci::Device& device): mm{&device} {
    device.set_privileges(pci::privileges::Mmio | pci::privileges::Dma);

    auto bar = device.read_bar(0);
    ASSERT(bar.type == pci::Bar::Type::Mmio);

    auto vector = idt::allocate_vector();
    device.enable_irq(vector);
    idt::set_handler(vector, idt::handler{.f = [](uint8_t, idt::regs*, void* userptr){
        auto& self = *(HDAController*)userptr;
        self.handle_irq();
    }, .is_irq = true, .should_iret = true, .userptr = this});

    vmm::kernel_vmm::get_instance().map(bar.base, bar.base + phys_mem_map, paging::mapPagePresent | paging::mapPageWrite, msr::pat::uc);

    regs = (volatile Regs*)(bar.base + phys_mem_map);

    uint16_t major = regs->vmaj, minor = regs->vmin;
    print("hda: Controller {}.{}\n", major, minor);

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
    hpet::poll_sleep(10);

    oss = (regs->gcap >> 12) & 0xF;
    iss = (regs->gcap >> 8) & 0xF;
    bss = (regs->gcap >> 3) & 0x1F;
    print("     OSS: {}, ISS: {}, BSS: {}\n", oss, iss, bss);

    stream_ids = std::bitmap{30}; // 30 Stream IDs to allocate
    oss_bitmap = std::bitmap{oss};
    iss_bitmap = std::bitmap{iss};
    bss_bitmap = std::bitmap{bss};

    in_descriptors = (volatile StreamDescriptor*)(bar.base + phys_mem_map + sizeof(Regs));
    out_descriptors = (volatile StreamDescriptor*)(bar.base + phys_mem_map + sizeof(Regs) + (iss * sizeof(StreamDescriptor)));
    bi_descriptors = (volatile StreamDescriptor*)(bar.base + phys_mem_map + sizeof(Regs) + (iss * sizeof(StreamDescriptor)) + (oss * sizeof(StreamDescriptor)));

    regs->intctl = (1 << 31) | (1 << 30); // Global IRQ enable + Controller IRQ enable
    regs->wakeen = 0x7FFF; // Allow all Codecs to generate Wake events
    regs->ssync = 0x3FFF'FFFF; // Don't start any stream

    auto calculate_ringbuffer_size = [&](uint8_t size) -> std::pair<uint8_t, size_t> {
        auto mask = (size >> 4) & 0xF;
        size_t index = 0;
        for(size_t i = 0; i < 3; i++)
            if(mask & (1 << 2))
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

uint32_t hda::HDAController::verb_get_parameter(uint8_t codec, uint8_t node, WidgetParameter param) {
    return corb_cmd(codec, node, (0xF00 << 8) | (uint8_t)param);
}

uint32_t hda::HDAController::verb_get_config_default(uint8_t codec, uint8_t node) {
    return corb_cmd(codec, node, (0xF1C << 8));
}

void hda::HDAController::verb_set_config_default(uint8_t codec, uint8_t node, uint32_t v) {
    ASSERT(corb_cmd(codec, node, (0x71C << 8) | (v & 0xFF)) == 0);
    ASSERT(corb_cmd(codec, node, (0x71D << 8) | ((v >> 8) & 0xFF)) == 0);
    ASSERT(corb_cmd(codec, node, (0x71E << 8) | ((v >> 16) & 0xFF)) == 0);
    ASSERT(corb_cmd(codec, node, (0x71F << 8) | ((v >> 24) & 0xFF)) == 0);
}

uint16_t hda::HDAController::verb_get_converter_format(uint8_t codec, uint8_t node) {
    return corb_cmd(codec, node, (0xA << 8)) & 0xFFFF;
}

void hda::HDAController::verb_set_converter_format(uint8_t codec, uint8_t node, uint16_t v) {
    ASSERT(corb_cmd(codec, node, (0x2 << 16) | v) == 0);
}

std::pair<uint8_t, uint8_t> hda::HDAController::verb_get_converter_control(uint8_t codec, uint8_t node) {
    auto res = corb_cmd(codec, node, (0xF06 << 8));

    return {(res >> 4) & 0xF, res & 0xF};
}
void hda::HDAController::verb_set_converter_control(uint8_t codec, uint8_t node, uint8_t stream, uint8_t channel) {
    uint8_t param = (stream << 4) | channel;
    ASSERT(corb_cmd(codec, node, (0x706 << 8) | param) == 0);
}

void hda::HDAController::verb_set_pin_control(uint8_t codec, uint8_t node, uint8_t v) {
    ASSERT(corb_cmd(codec, node, (0x707 << 8) | v) == 0);
}

void hda::HDAController::verb_set_eapd_control(uint8_t codec, uint8_t node, uint8_t v) {
    ASSERT(corb_cmd(codec, node, (0x70C << 8) | v) == 0);
}

void hda::HDAController::verb_set_dac_amplifier_gain(uint8_t codec, uint8_t node, uint16_t v) {
    ASSERT(corb_cmd(codec, node, (0x3 << 16) | v) == 0);
}

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

    for(auto& tmp : codec.widgets) {
        auto& widget = tmp.second;
        if(widget.type == 4) { // Pin Complex, Enumerate Paths
            Path path{};
            path.codec = index;
            path.pin = widget.nid;

            auto connectivity = (widget.config_default >> 30) & 0b11;
            if(connectivity == 0b00) path.loc = Path::Location::External;
            else if(connectivity == 0b10) path.loc = Path::Location::Internal;
            else if(connectivity == 0b11) path.loc = Path::Location::Both;
            else if(connectivity == 0b01) continue; // Nothing attached

            path.type = (widget.config_default >> 20) & 0xF;

            // TODO: Do this recursively, to resolve nested pin chains
            for(auto connection : widget.connection_list) {
                auto& conn_widget = codec.widgets[connection];

                if(conn_widget.type == 0) {
                    path.dac = conn_widget.nid;
                    break;
                }
            }

            if(path.dac == 0)
                continue; // Couldn't find a DAC

            codec.paths.push_back(path);
        }
    }

    for(auto& path : codec.paths) {
        print("     Device: {}\n", default_device_strs[path.type]);
    }
}

void hda::HDAController::enumerate_widget(uint8_t codec, uint8_t node, uint8_t function_group) {
    auto res = verb_get_parameter(codec, node, WidgetParameter::AudioWidgetCap);

    auto type = (res >> 20) & 0xF;
    print("       - Node {}: {}\n", (uint16_t)node, widget_type_strs[type]);

    Widget widget{};
    widget.nid = node;
    widget.type = type;
    widget.function_group = function_group;

    widget.cap = res;
    widget.pin_cap = verb_get_parameter(codec, node, WidgetParameter::PinCap);
    widget.in_amp_cap = verb_get_parameter(codec, node, WidgetParameter::InAmpCap);
    widget.out_amp_cap = verb_get_parameter(codec, node, WidgetParameter::OutAmpCap);
    
    widget.volume_knob_cap = verb_get_parameter(codec, node, WidgetParameter::VolumeKnobCap);

    widget.config_default = verb_get_config_default(codec, node);

    if(widget.cap & (1 << 8)) { // Connection list present
        res = verb_get_parameter(codec, node, WidgetParameter::ConnectionListLength);
        auto long_form = (res >> 7) & 1;
        uint8_t len = res & 0x7F;

        auto push = [&](uint16_t v) {
            if(v) // Connection list entries are only valid if they're non-null
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
        
        print("         Connection List: ");
        for(auto entry : widget.connection_list) 
            print("{} ", entry);
        print("\n");
    }

    codecs[codec].widgets[node] = widget;
}

void hda::HDAController::handle_irq() {
    if(regs->rirbsts & 1) {
        regs->rirbsts = 1; // Clear RIRB Overrun status if needed

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

bool hda::HDAController::stream_create(const hda::StreamParameters& params, size_t entries, Path& device, hda::Stream& stream) {
    stream.params = params;
    stream.device = &device;

    auto stream_id = stream_ids.get_free_bit();
    if(stream_id == ~0ull)
        return false; // Was not able to allocate StreamID
    stream_ids.set(stream_id);

    stream.index = stream_id;

    auto reset_stream = [&]() {
        stream.desc->ctl &= ~(1 << 1); // Clear DMA Run

        // This seems to be what linux does and the spec says, however it doesn't work on qemu
        /*stream.desc->ctl |= 1; // Enter Reset
        while(!(stream.desc->ctl & 1)) // Wait Device to enter reset state
            asm("pause");

        stream.desc->ctl &= ~1; // Leave Reset mode
        while(stream.desc->ctl & 1) // Wait Device to enter reset state
            asm("pause");*/
        
        stream.desc->ctl |= 1; // Set Reset
        while(stream.desc->ctl & 1) // Wait for reset to complete
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
                return false;
        } else {
            oss_bitmap.set(oss_id);

            stream.desc = &out_descriptors[oss_id];
            reset_stream();
        }
    } else {
        auto iss_id = iss_bitmap.get_free_bit();
        if(iss_id == ~0ull) {
            if(!try_bidir())
                return false;
        } else {
            iss_bitmap.set(iss_id);

            stream.desc = &in_descriptors[iss_id];
            reset_stream();
        }
    }

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
        return false; // Unknown sample size

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
    
    regs->ssync |= (1 << stream.index); // Make sure stream doesn't start

    verb_set_converter_format(device.codec, device.dac, stream.fmt);
    verb_set_converter_control(device.codec, device.dac, stream.index, 1);

    auto max_gain = (codecs[device.codec].widgets[device.dac].out_amp_cap >> 8) & 0x7F;
    verb_set_dac_amplifier_gain(device.codec, device.dac, (1 << 15) | (1 << 13) | (1 << 12) | max_gain);

    verb_set_pin_control(device.codec, device.pin, (1 << 6)); // Enable Pin Out
    if(codecs[device.codec].widgets[device.pin].pin_cap & (1 << 16)) // EAPD Capable
        verb_set_eapd_control(device.codec, device.pin, (1 << 1)); // Set EAPD

    return true;
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
    uint16_t stream_mask = 0;
    for(size_t i = 0; i < n_streams; i++) {
        streams[i]->desc->cbl = streams[i]->total_size;
        streams[i]->desc->ctl |= (1 << 1); // Start stream DMA Engine

        stream_mask |= (1 << streams[i]->index);
    }

    regs->ssync &= ~stream_mask; // GO

    return true;
}

hda::Path* hda::HDAController::get_device(uint8_t codec, uint8_t path) {
    if(codec >= 16 || path >= codecs[codec].paths.size())
        return nullptr;

    return &codecs[codec].paths[path];
}




struct {
    uint16_t vid, did;
} known_hda_devices[] = {
    {0x8086, 0x2668}, // Intel ICH6
    {0x8086, 0x293E}, // Intel ICH9
};

static std::linked_list<hda::HDAController> controllers;

void hda::init() {
    for(const auto [vid, did] : known_hda_devices) {
        pci::Device* dev = nullptr;
        size_t i = 0;
        while((dev = pci::device_by_id(vid, did, i++))) {
            controllers.emplace_back(*dev);
        }
    }

    /*auto& path = *controllers[0].get_device(0, 0);

    hda::Stream stream{};
    ASSERT(controllers[0].stream_create({.dir = true, .sample_rate = 44100, .sample_size = 16, .channels = 2}, 1, path, stream));

    auto* file = vfs::get_vfs().open("A:/music.pcm");
    auto size = file->get_size();

    uint8_t* data = new uint8_t[size];
    ASSERT(file->read(0, size, data) == size);

    ASSERT(controllers[0].stream_push(stream, size, data));

    Stream* streams[] = {
        &stream
    };
    ASSERT(controllers[0].streams_start(1, streams));

    delete[] data;
    file->close();*/
}