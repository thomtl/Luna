#pragma once

#include <Luna/common.hpp>
#include <Luna/fs/vfs.hpp>

#include <std/vector.hpp>

#include <Luna/cpu/regs.hpp>
#include <Luna/vmm/drivers.hpp>
#include <Luna/vmm/drivers/irqs/lapic.hpp>

namespace vm {
    struct RegisterState {
        uint64_t rax, rbx, rcx, rdx, rdi, rsi, rbp;
        uint64_t r8, r9, r10, r11, r12, r13, r14, r15;

        uint64_t rsp, rip, rflags;

        struct Segment {
            uint16_t selector;
            uint64_t base;
            uint32_t limit;
            struct {
                uint16_t type : 4 = 0;
                uint16_t s : 1 = 0;
                uint16_t dpl : 2 = 0;
                uint16_t present : 1 = 0;
                uint16_t avl : 1 = 0;
                uint64_t l : 1 = 0;
                uint16_t db : 1 = 0;
                uint16_t g : 1 = 0;
            } attrib;
        };
        Segment cs, ds, ss, es, fs, gs, ldtr, tr;

        struct Table {
            uint64_t base;
            uint16_t limit;
        };
        Table gdtr, idtr;

        uint64_t cr0, cr3, cr4;
        uint64_t efer;
        uint64_t dr0, dr1, dr2, dr3, dr6, dr7;
    };

    constexpr size_t max_x86_instruction_size = 15;
    struct VmExit {
        enum class Reason { Unknown, Hlt, Vmcall, MMUViolation, PIO, MSR, CPUID, RSM };
        static constexpr const char* reason_to_string(const Reason& reason) {
            switch (reason) {
                case Reason::Unknown: return "Unknown";
                case Reason::Hlt: return "HLT";
                case Reason::Vmcall: return "VM{M}CALL";
                case Reason::MMUViolation: return "MMUViolation";
                case Reason::PIO: return "Port IO";
                case Reason::MSR: return "MSR";
                case Reason::CPUID: return "CPUID";
                case Reason::RSM: return "RSM";
                default: return "Unknown";
            }
        }

        Reason reason;

        uint8_t instruction_len;
        uint8_t instruction[max_x86_instruction_size];

        union {
            struct {
                struct {
                    uint8_t present : 1;
                    uint8_t r : 1;
                    uint8_t w : 1;
                    uint8_t x : 1;
                    uint8_t user : 1;
                } access, page;
                bool reserved_bits_set;
                uint64_t gpa;
            } mmu;

            struct {
                uint8_t size;
                bool write, string, rep;
                uint16_t port;
            } pio;

            struct {
                bool write;
            } msr;
        };
    };

    struct AbstractMM {
        virtual ~AbstractMM() {}

        virtual void map(uintptr_t hpa, uintptr_t gpa, uint64_t flags) = 0;
        virtual uintptr_t unmap(uintptr_t gpa) = 0;
        virtual void protect(uintptr_t gpa, uint64_t flags) = 0;
        virtual uintptr_t get_phys(uintptr_t gpa) = 0;

        virtual uintptr_t get_root_pa() const = 0;
        virtual uint32_t get_asid() const = 0;
        virtual uint8_t get_levels() const = 0;
    };

    enum class VmCap { FullPIOAccess, SMMEntryCallback, SMMLeaveCallback };
    namespace VmRegs {
        enum {
            General = (1 << 0),
            Segment = (1 << 1),
            Control = (1 << 2)
        };
    } // namespace VmRegs
    

    struct AbstractVm {
        virtual ~AbstractVm() {}

        virtual void set(VmCap cap, bool v) = 0;
        virtual void get_regs(vm::RegisterState& regs, uint64_t flags) const = 0;
        virtual void set_regs(const vm::RegisterState& regs, uint64_t flags) = 0;
        
        virtual simd::Context& get_guest_simd_context() = 0;

        enum class InjectType { ExtInt, NMI, Exception, SoftwareInt };
        virtual void inject_int(InjectType type, uint8_t vector, bool error_code = false, uint32_t error = 0) = 0;

        virtual bool run(VmExit& exit) = 0;
    };

    struct Vm;

    struct VCPU {
        VCPU(Vm* vm, uint8_t id);

        
        void set(VmCap cap, bool value);
        void set(VmCap cap, void (*fn)(void*), void* userptr);
        
        void get_regs(vm::RegisterState& regs, uint64_t flags = VmRegs::General | VmRegs::Segment | VmRegs::Control) const;
        void set_regs(const vm::RegisterState& regs, uint64_t flags = VmRegs::General | VmRegs::Segment | VmRegs::Control);

        void enter_smm();
        void handle_rsm();

        void dma_write(uintptr_t gpa, std::span<uint8_t> buf);
        void dma_read(uintptr_t gpa, std::span<uint8_t> buf);

        void map(uintptr_t hpa, uintptr_t gpa, uint64_t flags);
        void protect(uintptr_t gpa, uint64_t flags);
        bool run();

        void update_mtrr(bool write, uint32_t index, uint64_t& val);

        struct {
            struct {
                uintptr_t base;
                uintptr_t mask;
            } var[8];

            uint64_t fix[11];
            uint64_t cmd;

            bool enable, fixed_enable;
            uint8_t default_type;
        } mtrr;
        uint64_t apicbase;
        uint64_t tsc;
        uint64_t smbase;

        bool is_in_smm;

        uint64_t cr0_constraint = 0, cr4_constraint = 0, efer_constraint = 0;

        Vm* vm;
        AbstractVm* vcpu;

        irqs::lapic::Driver lapic;

        void (*smm_entry_callback)(void*); void* smm_entry_userptr;
        void (*smm_leave_callback)(void*); void* smm_leave_userptr;
    };

    struct Vm {
        Vm(uint8_t n_cpus);
        std::unordered_map<uint16_t, AbstractPIODriver*> pio_map;
        std::unordered_map<uintptr_t, std::pair<AbstractMMIODriver*, size_t>> mmio_map;

        std::vector<VCPU> cpus;
        AbstractMM* mm;
    };

    void init();
} // namespace vm
