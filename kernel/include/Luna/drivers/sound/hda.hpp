#pragma once

#include <Luna/common.hpp>
#include <Luna/drivers/pci.hpp>
#include <Luna/mm/iovmm.hpp>

#include <std/vector.hpp>
#include <std/unordered_map.hpp>
#include <std/bitmap.hpp>
#include <std/optional.hpp>

namespace hda {
    enum class WidgetType : uint8_t {
        DAC = 0,
        ADC = 1,
        Mixer = 2,
        Selector = 3,
        PinComplex = 4,
        PowerWidget = 5,
        VolumeKnob = 6,
        BeepGenerator = 7,
        VendorDefined = 0xF,
    };

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
        VolumeKnobCap = 0x13
    };

    enum class Verb : uint16_t {
        GetParameter = 0xF00,

        GetConnectionSelect = 0xF01,
        SetConnectionSelect = 0x701,

        GetConnectionListEntry = 0xF02,

        GetProcessingState = 0xF03,
        SetProcessingState = 0x703,

        GetCoefficientIndex = 0xD,
        SetCoefficientIndex = 0x5,
        
        GetProcessingCoefficient = 0xC,
        SetProcessingCoefficient = 0x4,

        GetAmpGainMute = 0xB,
        SetAmpGainMute = 0x3,

        GetStreamFormat = 0xA,
        SetStreamFormat = 0x2,

        GetPowerState = 0xF05,
        SetPowerState = 0x705,

        GetChannelStreamID = 0xF06,
        SetChannelStreamID = 0x706,

        GetPinWidgetControl = 0xF07,
        SetPinWidgetControl = 0x707,

        GetPinSense = 0xF09,
        SetPinSense = 0x709,

        GetEAPDEnable = 0xF0C,
        SetEAPDEnable = 0x70C,

        GetConfigDefault = 0xF1C,
    };

    enum class ProcessingMode : uint8_t {
        Off = 0,
        On = 1,
        Benign = 2,
    };

    enum class PowerState {
        D0, D1, D2, D3hot, D3cold
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
        uint8_t codec_id, nid, function_group;
        WidgetType type;

        uint32_t cap, pin_cap, in_amp_cap, out_amp_cap, volume_knob_cap, processing_cap, config_default;
        uint32_t supported_power_states;
        std::vector<uint16_t> connection_list;
    };

    struct Path {
        uint8_t codec;
        std::vector<uint16_t> path;

        std::optional<uint16_t> nid_pin, nid_dac, nid_mixer;

        enum class Location { External, Internal, Both };
        constexpr static const char* location_to_str(Location loc) {
            using enum Location;
            switch(loc) {
                case External: return "External";
                case Internal: return "Internal";
                case Both: return "Internal / External";
                default: return "Unknown";
            }
        }

        Location loc;
        uint8_t type;
    };

    struct Codec {
        uint16_t vid, did;
        uint16_t version_major, verion_minor;

        uint16_t starting_node, n_nodes;

        std::unordered_map<uint8_t, Widget> widgets;
        std::vector<Path> paths;

        volatile bool have_response; // Is modified from IRQ context
        uint32_t curr_response; // TODO: This should probably be a FIFO
    };


    // TODO: This should probably be generic
    struct StreamParameters {
        bool dir;
        uint16_t sample_rate;
        uint8_t sample_size;
        uint8_t channels;
    };

    struct Stream {
        uint8_t stream_id;
        uint8_t stream_descriptor_index;
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

        HDAController(const HDAController&) = delete;
        HDAController& operator=(const HDAController&) = delete;

        HDAController(HDAController&&) = delete;
        HDAController& operator=(HDAController&&) = delete;

        std::optional<Stream> stream_create(const StreamParameters& params, size_t entries, Path& device);
        bool stream_push(Stream& stream, size_t size, uint8_t* pcm);
        bool streams_start(size_t n_streams, Stream** streams);

        Path* get_device(uint8_t codec, uint8_t path);

        private:
        void enumerate_codec(uint8_t index);
        void enumerate_widget(uint8_t codec, uint8_t node, uint8_t function_group);

        void handle_irq();

        uint32_t corb_cmd(uint8_t codec, uint8_t node, uint32_t cmd);

        void verb_set_pin_widget_control(const Widget& node, uint8_t v);
        void verb_set_eapd_enable(const Widget& node, uint8_t v);
        void verb_set_amp_gain_mute(const Widget& node, uint16_t v);
        void verb_set_processing_state(const Widget& node, ProcessingMode mode);
        void verb_set_connection_select(const Widget& node, uint8_t connection);
        void verb_set_coefficient(const Widget& node, uint16_t coefficient, uint16_t value);
        void verb_set_power_state(uint8_t codec, uint8_t node, uint8_t state);
        void verb_set_power_state(const Widget& node, uint8_t state);
        void verb_set_stream_format(const Widget& node, uint16_t v);
        void verb_set_converter_stream_channel(const Widget& node, uint8_t stream, uint8_t channel);

        uint32_t verb_get_parameter(uint8_t codec, uint8_t node, WidgetParameter param);
        uint32_t verb_get_parameter(const Widget& node, WidgetParameter param);
        uint16_t verb_get_coefficient(const Widget& node, uint16_t coefficient);
        uint32_t verb_get_power_state(const Widget& node);
        uint32_t verb_get_config_default(const Widget& node);

        uint32_t quirks;

        iovmm::Iovmm mm;
        volatile Regs* regs;
        volatile uint32_t* ssync;
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

        Codec codecs[15];
    };

    enum {
        quirkSnoopSCH = (1 << 0),
        quirkSnoopATI = (1 << 1),
        quirkNoSnoop = (1 << 2),
        quirkTCSEL = (1 << 3),
        quirkOldSsync = (1 << 4),
    };
} // namespace hda
