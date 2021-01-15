#pragma once

#include <Luna/common.hpp>
#include <Luna/drivers/pci.hpp>
#include <Luna/mm/iovmm.hpp>

#include <std/vector.hpp>
#include <std/unordered_map.hpp>
#include <std/bitmap.hpp>

namespace hda {
    enum class WidgetParameter : uint8_t {
        VendorID = 0,
        RevisionID = 2,
        SubordinateNodeCount = 4,
        FunctionGroupType = 5,
        AudioFunctionGroupCap = 8,
        AudioWidgetCap = 9,
        SampleSizeRateCap = 0xA,
        StreamFormats = 0xB,
        PinCap = 0xC,
        InAmpCap = 0xD,
        OutAmpCap = 0x12,
        ConnectionListLength = 0xE,
        SupportedPowerStates = 0xF,
        ProcessingCap = 0x10,
        GPIOCount = 0x11,
        VolumeKnobCap = 0x12
    };

    struct [[gnu::packed]] Regs {
        uint16_t gcap;
        uint8_t vmin;
        uint8_t vmaj;
        uint16_t outpay;
        uint16_t inpay;
        uint32_t gctl;
        uint16_t wakeen;
        uint16_t statests;
        uint16_t gsts;
        uint8_t reserved[6];
        uint16_t outstrmpay;
        uint16_t instrmpay;
        uint32_t reserved_0;
        uint32_t intctl;
        uint32_t insts;
        uint64_t reserved_1;
        uint32_t walclk;
        uint32_t old_ssync;
        uint32_t ssync;
        uint32_t reserved_3;
        uint32_t corblbase;
        uint32_t corbubase;
        uint16_t corbwp;
        uint16_t corbrp;
        uint8_t corbctl;
        uint8_t corbsts;
        uint8_t corbsize;
        uint8_t reserved_4;
        uint32_t rirblbase;
        uint32_t rirbubase;
        uint16_t rirbwp;
        uint16_t rirbcnt;
        uint8_t rirbctl;
        uint8_t rirbsts;
        uint8_t rirbsize;
        uint8_t reserved_5;
        uint32_t icoi;
        uint32_t icii;
        uint16_t icis;
        uint8_t reserved_6[6];
        uint32_t dpiblbase;
        uint32_t bpibubase;
        uint64_t reserved_7;
    };
    static_assert(sizeof(Regs) == 0x80);

    struct [[gnu::packed]] StreamDescriptor {
        uint16_t ctl;
        uint8_t ctl_high;
        uint8_t sts;
        uint32_t lpib;
        uint32_t cbl;
        uint16_t lvi;
        uint16_t reserved;
        uint16_t fifod;
        uint16_t fmt;
        uint32_t reserved_0;
        uint32_t bdpl;
        uint32_t bdpu;
    };
    static_assert(sizeof(StreamDescriptor) == 0x20);

    struct [[gnu::packed]] BufferDescriptor {
        uint64_t addr;
        uint32_t len;
        uint32_t flags;
    };

    struct [[gnu::packed]] BufferDescriptorList {
        BufferDescriptor desc[0];

        auto& operator[](size_t i) { return desc[i]; }
    };

    struct Widget {
        uint8_t nid, type, function_group;

        uint32_t cap, pin_cap, in_amp_cap, out_amp_cap, volume_knob_cap, config_default;
        std::vector<uint16_t> connection_list;
    };

    struct Path {
        uint8_t codec;
        uint8_t pin, dac;
        enum class Location { External, Internal, Both };
        Location loc;
        uint8_t type;
    };

    // TODO: This should probably be generic
    struct StreamParameters {
        bool dir;
        uint16_t sample_rate;
        uint8_t sample_size;
        uint8_t channels;
    };

    struct Stream {
        uint8_t index;
        size_t entries, entries_i, total_size;
        
        StreamParameters params;

        uint16_t fmt;
        Path* device;

        iovmm::Iovmm::Allocation bdl_alloc;
        std::vector<iovmm::Iovmm::Allocation> bdle_allocs;
        
        volatile StreamDescriptor* desc;
        volatile BufferDescriptorList* bdl;
    };

    struct HDAController {
        HDAController(pci::Device& device, uint16_t vid, uint32_t quirks);
        ~HDAController();

        HDAController(const HDAController& src) = delete;

        HDAController(HDAController&&) = delete;
        HDAController& operator=(HDAController) = delete;

        bool stream_create(const StreamParameters& params, size_t entries, Path& device, Stream& stream);
        bool stream_push(Stream& stream, size_t size, uint8_t* pcm);
        bool streams_start(size_t n_streams, Stream** streams);

        Path* get_device(uint8_t codec, uint8_t path);

        private:
        void enumerate_codec(uint8_t index);
        void enumerate_widget(uint8_t codec, uint8_t node, uint8_t function_group);

        void handle_irq();

        uint32_t corb_cmd(uint8_t codec, uint8_t node, uint32_t cmd);

        uint32_t verb_get_parameter(uint8_t codec, uint8_t node, WidgetParameter param);
        uint32_t verb_get_config_default(uint8_t codec, uint8_t node);
        void verb_set_config_default(uint8_t codec, uint8_t node, uint32_t v);
        uint16_t verb_get_converter_format(uint8_t codec, uint8_t node);
        void verb_set_converter_format(uint8_t codec, uint8_t node, uint16_t v);
        std::pair<uint8_t, uint8_t> verb_get_converter_control(uint8_t codec, uint8_t node);
        void verb_set_converter_control(uint8_t codec, uint8_t node, uint8_t stream, uint8_t channel);
        void verb_set_pin_control(uint8_t codec, uint8_t node, uint8_t v);
        void verb_set_eapd_control(uint8_t codec, uint8_t node, uint8_t v);
        void verb_set_dac_amplifier_gain(uint8_t codec, uint8_t node, uint16_t v);

        void update_ssync(bool set, uint32_t mask);

        uint32_t quirks;

        iovmm::Iovmm mm;
        volatile Regs* regs;
        volatile StreamDescriptor* in_descriptors;
        volatile StreamDescriptor* out_descriptors;
        volatile StreamDescriptor* bi_descriptors;

        std::bitmap stream_ids, iss_bitmap, oss_bitmap, bss_bitmap;

        uint16_t iss, oss, bss;
        bool a64;

        struct {
            size_t size;
            volatile uint32_t* corb;
            iovmm::Iovmm::Allocation alloc;
        } corb;

        struct {
            size_t size, head;
            volatile uint32_t* rirb;
            iovmm::Iovmm::Allocation alloc;
        } rirb;

        struct {
            uint16_t vid, did;
            uint16_t version_major, verion_minor;

            uint16_t starting_node, n_nodes;

            std::unordered_map<uint8_t, Widget> widgets;
            std::vector<Path> paths;

            volatile bool have_response; // Is modified from IRQ context
            uint32_t curr_response; // TODO: This should probably be a FIFO
        } codecs[15];
    };

    enum {
        quirkSnoopSCH = (1 << 0),
        quirkSnoopATI = (1 << 1),
        quirkNoSnoop = (1 << 2),
        quirkNoTCSEL = (1 << 3),
        quirkOldSsync = (1 << 4),
    };

    void init();
} // namespace hda
