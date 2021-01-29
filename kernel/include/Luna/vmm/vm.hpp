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
        enum class Reason { Unknown, Hlt, Vmcall, MMUViolation, PIO, MSR, CPUID };
        static constexpr const char* reason_to_string(const Reason& reason) {
            switch (reason) {
                case Reason::Unknown: return "Unknown";
                case Reason::Hlt: return "HLT";
                case Reason::Vmcall: return "VM{M}CALL";
                case Reason::MMUViolation: return "MMUViolation";
                case Reason::PIO: return "Port IO";
                case Reason::MSR: return "MSR";
                case Reason::CPUID: return "CPUID";
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
        virtual uintptr_t unmap(uintptr_t gpa);
        virtual void protect(uintptr_t gpa, uint64_t flags) = 0;
        virtual uintptr_t get_phys(uintptr_t gpa) = 0;

        virtual uintptr_t get_root_pa() const = 0;
        virtual uint32_t get_asid() const = 0;
        virtual uint8_t get_levels() const = 0;
    };

    enum class VmCap { FullPIOAccess };

    struct AbstractVm {
        virtual ~AbstractVm() {}

        virtual void set(VmCap cap, bool v) = 0;
        virtual void get_regs(vm::RegisterState& regs) const = 0;
        virtual void set_regs(const vm::RegisterState& regs) = 0;
        
        virtual simd::Context& get_guest_simd_context() = 0;

        enum class InjectType { ExtInt, NMI, Exception, SoftwareInt };
        virtual void inject_int(InjectType type, uint8_t vector, bool error_code = false, uint32_t error = 0) = 0;

        virtual bool run(VmExit& exit) = 0;
    };

    struct Vm;

    struct VCPU {
        VCPU(Vm* vm, uint8_t id);

        
        void set(VmCap cap, bool value);
        
        void get_regs(vm::RegisterState& regs) const;
        void set_regs(const vm::RegisterState& regs);

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

        Vm* vm;
        AbstractVm* vcpu;

        irqs::lapic::Driver lapic;
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
