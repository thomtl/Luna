#pragma once

#include <Luna/common.hpp>
#include <std/concepts.hpp>
#include <Luna/mm/iovmm.hpp>

namespace xhci {
    namespace trb_types {
        constexpr uint8_t normal = 1;
        constexpr uint8_t setup = 2;
        constexpr uint8_t data = 3;
        constexpr uint8_t status = 4;
        constexpr uint8_t isoch = 5;
        constexpr uint8_t link = 6;
        constexpr uint8_t event_data = 7;
        constexpr uint8_t no_op = 8;

        constexpr uint8_t enable_slot_cmd = 9;
        constexpr uint8_t disable_slot_cmd = 10;
        constexpr uint8_t address_device_cmd = 11;
        constexpr uint8_t configure_ep_cmd = 12;
        constexpr uint8_t evaluate_context_cmd = 13;
        constexpr uint8_t reset_ep_cmd = 14;
        constexpr uint8_t stop_ep_cmd = 15;
        constexpr uint8_t set_tr_dequeue_cmd = 16;
        constexpr uint8_t reset_device_cmd = 17;
        constexpr uint8_t force_event_cmd = 18;
        constexpr uint8_t negotiate_bandwidth_cmd = 19;
        constexpr uint8_t set_latency_cmd = 20;
        constexpr uint8_t get_bandwidth_cmd = 21;
        constexpr uint8_t force_header_cmd = 22;
        constexpr uint8_t no_op_cmd = 23;
        constexpr uint8_t get_properties_cmd = 24;
        constexpr uint8_t set_properties_cmd = 25;

        constexpr uint8_t transfer_evt = 32;
        constexpr uint8_t cmd_completion_evt = 33;
        constexpr uint8_t status_change_evt = 34;
    } // namespace trb_types

    namespace trb_codes {
        constexpr uint8_t success = 1;
        constexpr uint8_t short_packet = 13;
    } // namespace trb_codes
    
    
    template<typename T>
    concept TRB = requires(T t) {
        { t.cycle } -> std::convertible_to<uint8_t>;
        { t.type } -> std::convertible_to<uint8_t>;
    } && (sizeof(T) == 16);

    struct [[gnu::packed]] TRBLink {
        uint64_t ptr;
        
        uint32_t reserved : 22;
        uint32_t interrupter : 10;

        uint32_t cycle : 1;
        uint32_t toggle_cycle : 1;
        uint32_t reserved_0 : 2;
        uint32_t chain : 1;
        uint32_t ioc : 1;
        uint32_t reserved_1 : 4;
        uint32_t type : 6;
        uint32_t reserved_2 : 16;
    };
    static_assert(TRB<TRBLink>);

    struct [[gnu::packed]] TRBNormal {
        uint64_t data_buf;

        uint32_t len : 17;
        uint32_t td_size : 5;
        uint32_t interrupter : 10;

        uint32_t cycle : 1;
        uint32_t eval_next_trb : 1;
        uint32_t irq_on_short_packet : 1;
        uint32_t no_snoop : 1;
        uint32_t chain : 1;
        uint32_t ioc : 1;
        uint32_t immediate_data : 1;
        uint32_t reserved : 2;
        uint32_t block_event : 1;
        uint32_t type : 6;
        uint32_t reserved_0 : 16;
    };
    static_assert(TRB<TRBNormal>);

    struct [[gnu::packed]] TRBSetup {
        uint32_t bmRequestType : 8;
        uint32_t bType : 8;
        uint32_t wValue : 16;

        uint32_t wIndex : 16;
        uint32_t wLength : 16;

        uint32_t len : 17;
        uint32_t td_size : 5;
        uint32_t interrupter : 10;

        uint32_t cycle : 1;
        uint32_t reserved : 4;
        uint32_t ioc : 1;
        uint32_t immediate : 1;
        uint32_t reserved_0 : 3;
        uint32_t type : 6;
        uint32_t transfer_type : 2;
        uint32_t reserved_1 : 14;
    };
    static_assert(TRB<TRBSetup>);

    struct [[gnu::packed]] TRBData {
        uint64_t buf;

        uint32_t len : 17;
        uint32_t td_size : 5;
        uint32_t interrupter : 10;

        uint32_t cycle : 1;
        uint32_t eval_next_trb : 1;
        uint32_t isp : 1;
        uint32_t no_snoop : 1;
        uint32_t chain : 1;
        uint32_t ioc : 1;
        uint32_t immediate_data : 1;
        uint32_t reserved : 3;
        uint32_t type : 6;
        uint32_t direction : 1;
        uint32_t reserved_0 : 15;
    };
    static_assert(TRB<TRBData>);

    struct [[gnu::packed]] TRBStatus {
        uint64_t reserved;

        uint32_t reserved_0 : 22;
        uint32_t interrupter : 10;

        uint32_t cycle : 1;
        uint32_t eval_next_trb : 1;
        uint32_t reserved_1 : 2;
        uint32_t chain : 1;
        uint32_t ioc : 1;
        uint32_t reserved_2 : 4;
        uint32_t type : 6;
        uint32_t direction : 1;
        uint32_t reserved_3 : 15;
    };
    static_assert(TRB<TRBData>);

    struct [[gnu::packed]] TRBCmdCompletionEvt {
        uint64_t cmd_ptr;

        uint32_t reserved : 24;
        uint32_t code : 8;

        uint32_t cycle : 1;
        uint32_t reserved_0 : 9;
        uint32_t type : 6;
        uint32_t vf_id : 8;
        uint32_t slot_id : 8;
    };
    static_assert(TRB<TRBCmdCompletionEvt>);

    struct [[gnu::packed]] TRBXferCompletionEvt {
        uint64_t cmd_ptr;

        uint32_t len : 24;
        uint32_t code : 8;
        
        uint32_t cycle : 1;
        uint32_t reserved : 1;
        uint32_t ed : 1;
        uint32_t reserved_0 : 7;
        uint32_t type : 6;
        uint32_t epid : 5;
        uint32_t reserved_1 : 3;
        uint32_t slot_id : 8;
    };
    static_assert(TRB<TRBXferCompletionEvt>);

    struct [[gnu::packed]] TRBCmdNoOp {
        uint32_t reserved[3];

        uint32_t cycle : 1;
        uint32_t reserved_0 : 9;
        uint32_t type : 6;
        uint32_t reserved_1 : 16;
    };
    static_assert(TRB<TRBCmdNoOp>);

    struct [[gnu::packed]] TRBCmdEnableSlot {
        uint32_t reserved[3];

        uint32_t cycle : 1;
        uint32_t reserved_0 : 9;
        uint32_t type : 6;
        uint32_t slot_type : 5;
        uint32_t reserved_1 : 11;
    };
    static_assert(TRB<TRBCmdEnableSlot>);

    struct [[gnu::packed]] TRBCmdAddressDevice {
        uint64_t input_ctx;
        uint32_t reserved;

        uint32_t cycle : 1;
        uint32_t reserved_0 : 8;
        uint32_t bsr : 1;
        uint32_t type : 6;
        uint32_t reserved_1 : 8;
        uint32_t slot_id : 8;
    };
    static_assert(TRB<TRBCmdAddressDevice>);

    struct [[gnu::packed]] TRBCmdEvaluateContext {
        uint64_t input_ctx;
        uint32_t reserved;

        uint32_t cycle : 1;
        uint32_t reserved_0 : 8;
        uint32_t bsr : 1;
        uint32_t type : 6;
        uint32_t reserved_1 : 8;
        uint32_t slot_id : 8;
    };
    static_assert(TRB<TRBCmdEvaluateContext>);

    struct [[gnu::packed]] TRBCmdConfigureEP {
        uint64_t input_ctx;
        uint32_t reserved;

        uint32_t cycle : 1;
        uint32_t reserved_0 : 8;
        uint32_t dc : 1;
        uint32_t type : 6;
        uint32_t reserved_1 : 8;
        uint32_t slot_id : 8;
    };
    static_assert(TRB<TRBCmdConfigureEP>);

    struct [[gnu::packed]] TRBRaw {
        uint32_t reserved[3];

        uint32_t cycle : 1;
        uint32_t reserved_0 : 9;
        uint32_t type : 6;
        uint32_t reserved_1 : 16;
    };
    static_assert(TRB<TRBRaw>);

    enum class RingType { Transfer, Command, Event };

    struct TRBRing {
        TRBRing(iovmm::Iovmm& mm, size_t n_entries, RingType type): mm{&mm}, n_entries{n_entries}, type{type} {
            size_t size = n_entries * 16;

            alloc = mm.alloc(size, (type == RingType::Event) ? (iovmm::Iovmm::DeviceToHost) : (iovmm::Iovmm::HostToDevice));

            ring = alloc.host_base;
            enqueue_ptr = 0;
            dequeue_ptr = 0;
            cycle = 1;

            // Non event rings use link TRBs to make them circular
            if(type != RingType::Event) {
                this->n_entries--; // Make sure we do not touch the Link TRB

                link = (TRBLink*)((ring + size) - 16); // Last entry

                link->type = trb_types::link;
                link->ptr = alloc.guest_base;
                link->toggle_cycle = 1;
                link->cycle = 1;
            }
        }

        ~TRBRing() {
            mm->free(alloc);
        }

        TRBRing(const TRBRing&) = delete;
        TRBRing& operator=(const TRBRing&) = delete;

        template<TRB T>
        size_t enqueue(const T& item) {
            auto return_i = enqueue_ptr;
            T* dst = (T*)(ring + (enqueue_ptr * 16));

            memcpy(dst, (const uint8_t*)&item, 16);
            dst->cycle = cycle;

            enqueue_ptr = (enqueue_ptr + 1) % n_entries;
            if(enqueue_ptr == 0) { // We just wrapped around
                link->cycle = cycle;
                cycle ^= 1;
            }

            return return_i;
        }

        uintptr_t get_guest_base() const { return alloc.guest_base; }
        size_t& get_cycle() { return cycle; }

        TRBRaw* peek_dequeue() { return (TRBRaw*)(ring + (dequeue_ptr * 16)); }
        TRBRaw* dequeue() { dequeue_ptr = (dequeue_ptr + 1) % n_entries; return (TRBRaw*)(ring + (dequeue_ptr * 16)); }
        uint64_t get_dequeue_ptr() const { return dequeue_ptr; }
        uint64_t get_enqueue_ptr() const { return enqueue_ptr; }

        void reset() {
            enqueue_ptr = 0;
            dequeue_ptr = 0;
            cycle = 1;

            for(size_t i = 0; i < n_entries; i++) {
                auto* trb = (TRBRaw*)(ring + (dequeue_ptr * 16));
                trb->cycle = 0;
            }
        
        }

        protected:
        iovmm::Iovmm* mm;
        iovmm::Iovmm::Allocation alloc;

        size_t enqueue_ptr, dequeue_ptr, n_entries, cycle;
        RingType type;

        volatile uint8_t* ring;
        volatile TRBLink* link;
    };


    struct CmdRing : public TRBRing {
        CmdRing(iovmm::Iovmm& mm, size_t n_entries, volatile uint32_t* doorbell): TRBRing{mm, n_entries, RingType::Command}, completion{}, doorbell{doorbell} { 
            completion.resize(this->n_entries); // Use the member, not the variable, they are different due to the TRBLink
        }

        template<TRB T>
        TRBCmdCompletionEvt issue(const T& item) {
            auto i = enqueue(item);
            completion[i].done = false;

            *doorbell = 0;
            while(!completion[i].done)
                asm("pause");

            return completion[i].trb;
        }

        void complete(size_t i, TRBCmdCompletionEvt& evt) {
            ASSERT(completion[i].done == false);

            completion[i].trb = evt;
            completion[i].done = true;
        }

        private:
        struct Completion {
            volatile bool done;
            TRBCmdCompletionEvt trb;
        };
        std::vector<Completion> completion;
        volatile uint32_t* doorbell;
    };

    struct TransferRing : public TRBRing {
        TransferRing(iovmm::Iovmm& mm, size_t n_entries, volatile uint32_t* doorbell): TRBRing{mm, n_entries, RingType::Command}, completion{}, doorbell{doorbell} { 
            completion.resize(this->n_entries); // Use the member, not the variable, they are different due to the TRBLink
        }

        TRBXferCompletionEvt run(size_t i, uint32_t db_val) {
            completion[i].done = false;

            *doorbell = db_val;
            while(!completion[i].done)
                asm("pause");

            return completion[i].trb;
        }

        TRBXferCompletionEvt run_await(size_t i, uint32_t db_val) {
            completion[i].done = false;

            *doorbell = db_val;
            completion[i].promise.await();

            return completion[i].trb;
        }

        void complete(size_t i, TRBXferCompletionEvt& evt) {
            ASSERT(completion[i].done == false);

            completion[i].trb = evt;
            completion[i].done = true;
            completion[i].promise.set_value(true);
        }

        private:
        struct Completion {
            volatile bool done;
            Promise<bool> promise;
            TRBXferCompletionEvt trb;
        };
        std::vector<Completion> completion;
        volatile uint32_t* doorbell;
    };
} // namespace xhci
