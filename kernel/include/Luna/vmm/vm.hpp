#pragma once

#include <Luna/common.hpp>
#include <Luna/fs/vfs.hpp>

#include <std/vector.hpp>

#include <Luna/cpu/regs.hpp>

namespace vm {
    struct RegisterState {
        uint64_t rax, rbx, rcx, rdx, rdi, rsi, rbp;
        uint64_t r8, r9, r10, r11, r12, r13, r14, r15;

        uint64_t rsp, rip, rflags;

        struct Segment {
            uint16_t selector;
            uint64_t base;
            uint32_t limit;
            uint16_t attrib;
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
        enum class Reason { Unknown, Hlt, Vmcall, MMUViolation, PIO, CPUID };
        static constexpr const char* reason_to_string(const Reason& reason) {
            switch (reason) {
                case Reason::Unknown: return "Unknown";
                case Reason::Hlt: return "HLT";
                case Reason::Vmcall: return "VM{M}CALL";
                case Reason::MMUViolation: return "MMUViolation";
                case Reason::PIO: return "Port IO";
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
        };
    };

    struct Vm;

    struct AbstractPIODriver {
        virtual ~AbstractPIODriver() {}

        virtual void register_pio_driver(Vm* vm) = 0;

        virtual void pio_write(uint16_t port, uint32_t value, uint8_t size) = 0;
        virtual uint32_t pio_read(uint16_t port, uint8_t size) = 0;
    };

    struct AbstractMMIODriver {
        virtual ~AbstractMMIODriver() {}

        virtual void register_mmio_driver(Vm* vm) = 0;

        virtual void mmio_write(uintptr_t addr, uint64_t value, uint8_t size) = 0;
        virtual uint64_t mmio_read(uintptr_t addr, uint8_t size) = 0;
    };


    struct AbstractVm {
        virtual ~AbstractVm() {}

        virtual void get_regs(vm::RegisterState& regs) const = 0;
        virtual void set_regs(const vm::RegisterState& regs) = 0;

        virtual void map(uintptr_t hpa, uintptr_t gpa, uint64_t flags) = 0;
        virtual uintptr_t get_phys(uintptr_t gpa) = 0;
        virtual simd::Context& get_guest_simd_context() = 0;

        virtual bool run(VmExit& exit) = 0;
    };

    struct Vm {
        Vm();
        
        void get_regs(vm::RegisterState& regs) const;
        void set_regs(const vm::RegisterState& regs);

        void map(uintptr_t hpa, uintptr_t gpa, uint64_t flags);
        bool run();

        std::vector<vfs::File*> disks;

        std::vector<AbstractPIODriver*> drivers;
        std::unordered_map<uint16_t, AbstractPIODriver*> pio_map;
        std::unordered_map<uintptr_t, std::pair<AbstractMMIODriver*, size_t>> mmio_map;


        private:
        AbstractVm* vm;

    };

    void init();
} // namespace vm
