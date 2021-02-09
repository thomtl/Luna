#pragma once

#include <Luna/common.hpp>
#include <Luna/mm/iovmm.hpp>

namespace xhci {
    struct [[gnu::packed]] SlotContext {
        uint32_t route_string : 20;
        uint32_t speed : 4;
        uint32_t reserved : 1;
        uint32_t mtt : 1;
        uint32_t hub : 1;
        uint32_t context_entries : 5;

        uint32_t max_exit_latency : 16;
        uint32_t root_hub_port_num : 8;
        uint32_t num_ports : 8;

        uint32_t tt_hub_slot_id : 8;
        uint32_t tt_hub_slot_num : 8;
        uint32_t tt_think_time : 2;
        uint32_t reserved_0 : 4;
        uint32_t interrupter : 10;

        uint32_t device_address : 8;
        uint32_t reserved_1 : 19;
        uint32_t state : 5;

        uint32_t reserved_2;
        uint32_t reserved_3;
        uint32_t reserved_4;
        uint32_t reserved_5;
    };
    static_assert(sizeof(SlotContext) == 32);

    struct [[gnu::packed]] EndpointContext {
        uint32_t state : 3;
        uint32_t reserved : 5;
        uint32_t mult : 2;
        uint32_t max_primary_streams : 5;
        uint32_t linear_stream_array : 1;
        uint32_t interval : 8;
        uint32_t max_esit_high : 8;

        uint32_t reserved_0 : 1;
        uint32_t error_count : 2;
        uint32_t ep_type : 3;
        uint32_t reserved_1 : 1;
        uint32_t host_initiate_disable : 1;
        uint32_t max_burst_size : 8;
        uint32_t max_packet_size : 16;

        uint64_t tr_dequeue;

        uint32_t average_trb_len : 16;
        uint32_t max_esit_low : 16;

        uint32_t reserved_3[3];
    };
    static_assert(sizeof(EndpointContext) == 32);

    namespace ep_types {
        constexpr uint8_t isoch_out = 1;
        constexpr uint8_t bulk_out = 2;
        constexpr uint8_t interrupt_out = 3;
        constexpr uint8_t control_bi = 4;
        constexpr uint8_t isoch_in = 5;
        constexpr uint8_t bulk_in = 6;
        constexpr uint8_t interrupt_in = 7;
    } // namespace ep_types
    

    struct InputControlContext {
        uint32_t drop_flags;
        uint32_t add_flags;
        uint32_t reserved[5];

        uint32_t config : 8;
        uint32_t interface : 8;
        uint32_t alternate : 8;
        uint32_t reserved_0 : 8;
    };

    /*
        - Slot Context
        - EP0 Context
        - EP1 Out Context
        - EP1 In Context
        ...
        - EP15 Out Context
        - EP15 In Context
    */
    struct DeviceContext {
        DeviceContext(iovmm::Iovmm& mm, size_t size): context_size{size}, mm{&mm} {
            ASSERT(size == 32 || size == 64);

            alloc = mm.alloc(size * 32, iovmm::Iovmm::Bidirectional);

        }

        ~DeviceContext() {
            mm->free(alloc);
        }

        DeviceContext(const DeviceContext&) = delete;
        DeviceContext& operator=(const DeviceContext&) = delete;

        SlotContext& get_slot_ctx() { return *(SlotContext*)alloc.host_base; }
        EndpointContext& get_ep0_ctx() { return *(EndpointContext*)(alloc.host_base + context_size); }
        EndpointContext& get_ep_ctx(uint8_t ep, bool in) { return *(EndpointContext*)(alloc.host_base + (ep * (context_size * 2)) + ((in ? 1 : 0) * context_size)); }

        uintptr_t get_guest_base() const { return alloc.guest_base; }

        private:
        size_t context_size;

        iovmm::Iovmm* mm;
        iovmm::Iovmm::Allocation alloc;
    };

    /*
        - Input Control Context
        - Slot Context
        - EP0 Context
        - EP1 Out Context
        - EP1 In Context
        ...
        - EP15 Out Context
        - EP15 In Context
    */
    struct InputContext {
        InputContext(iovmm::Iovmm& mm, size_t size): context_size{size}, mm{&mm} {
            ASSERT(size == 32 || size == 64);

            alloc = mm.alloc(size * 33, iovmm::Iovmm::HostToDevice);

        }

        ~InputContext() {
            mm->free(alloc);
        }

        InputContext(const InputContext&) = delete;
        InputContext& operator=(const InputContext&) = delete;

        InputControlContext& get_in_ctx() { return *(InputControlContext*)alloc.host_base; }
        SlotContext& get_slot_ctx() { return *(SlotContext*)(alloc.host_base + context_size); }
        EndpointContext& get_ep0_ctx() { return *(EndpointContext*)(alloc.host_base + (2 * context_size)); }
        EndpointContext& get_ep_ctx(uint8_t ep, bool in) { return *(EndpointContext*)(alloc.host_base + context_size + (ep * (context_size * 2)) + ((in ? 1 : 0) * context_size)); }

        uintptr_t get_guest_base() const { return alloc.guest_base; }

        private:
        size_t context_size;

        iovmm::Iovmm* mm;
        iovmm::Iovmm::Allocation alloc;
    };
} // namespace xhci
