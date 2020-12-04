#include <Luna/vmm/vm.hpp>

#include <Luna/cpu/intel/vmx.hpp>

enum class Vendor { Intel };
static Vendor vendor;

void vm::init() {
    if(vmx::is_supported()) {
        vendor = Vendor::Intel;

        vmx::init();
    } else 
        PANIC("Unknown virtualization vendor");
}

vm::Vm::Vm() {
    uint64_t cr0_constraint = 0, cr4_constraint = 0;
    switch (vendor) {
        case Vendor::Intel:
            vm = new vmx::Vm{};

            cr0_constraint = vmx::get_cr0_constraint();
            cr4_constraint = vmx::get_cr4_constraint();
            break;
        default:
            PANIC("Unknown virtualization vendor");
    }

    vm::RegisterState regs{};

    constexpr uint16_t code_access = 0b11 | (1 << 4) | (1 << 7) | (1 << 13);
    constexpr uint16_t data_access = 0b11 | (1 << 4) | (1 << 7);
    constexpr uint16_t ldtr_access = 0b10 | (1 << 7);
    constexpr uint16_t tr_access = 0b11 | (1 << 7);

    regs.cs = {.selector = 0, .base = 0, .limit = 0xFFFF, .attrib = code_access};

    regs.ds = {.selector = 0, .base = 0, .limit = 0xFFFF, .attrib = data_access};
    regs.es = {.selector = 0, .base = 0, .limit = 0xFFFF, .attrib = data_access};
    regs.ss = {.selector = 0, .base = 0, .limit = 0xFFFF, .attrib = data_access};
    regs.fs = {.selector = 0, .base = 0, .limit = 0xFFFF, .attrib = data_access};
    regs.gs = {.selector = 0, .base = 0, .limit = 0xFFFF, .attrib = data_access};

    regs.tr = {.selector = 0, .base = 0, .limit = 0xFFFF, .attrib = tr_access};
    regs.ldtr = {.selector = 0, .base = 0, .limit = 0xFFFF, .attrib = ldtr_access};

    regs.idtr = {.base = 0, .limit = 0xFFFF};
    regs.gdtr = {.base = 0, .limit = 0xFFFF};;

    regs.dr7 = 0;
    regs.rsp = 0;
    regs.rip = 0x1000;
    regs.rflags = (1 << 1);

    regs.cr0 = (cr0_constraint & ~((1 << 0) | (1 << 31))); // Clear PE and PG;
    regs.cr4 = cr4_constraint;

    regs.cr3 = 0;
    regs.efer = 0;

    vm->set_regs(regs);
}
        
void vm::Vm::get_regs(vm::RegisterState& regs) const { vm->get_regs(regs); }
void vm::Vm::set_regs(const vm::RegisterState& regs) { vm->set_regs(regs); }

void vm::Vm::map(uintptr_t hpa, uintptr_t gpa, uint64_t flags) { vm->map(hpa, gpa, flags); }
bool vm::Vm::run(vm::VmExit& exit) { return vm->run(exit); }