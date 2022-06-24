#include <Luna/drivers/gpu/lfb/vbe.hpp>
#include <Luna/vmm/vm.hpp>
#include <Luna/cpu/paging.hpp>
#include <Luna/drivers/pci.hpp>

/*#include <Luna/net/luna_debug.hpp>

struct MMIOCapture final : public vm::AbstractMMIODriver {
    MMIOCapture(uintptr_t base, size_t len): base{base}, len{len} {}

    void register_mmio_driver(vm::Vm* vm) {
        this->vm = vm;

        vm->mmio_map[base] = {this, len};
    }

    void mmio_write(uintptr_t addr, uint64_t value, uint8_t size) {
        format::format_to(net::luna_debug::Writer{}, "w {:#x} {:#x} {}", addr, value, (uint16_t)size);

        if(size == 8)
            *(volatile uint64_t*)(addr + phys_mem_map) = value;
        else if(size == 4)
            *(volatile uint32_t*)(addr + phys_mem_map) = value;
        else if(size == 2)
            *(volatile uint16_t*)(addr + phys_mem_map) = value;
        else if(size == 1)
            *(volatile uint8_t*)(addr + phys_mem_map) = value;
        else
            PANIC("Unknown size");
    }

    uint64_t mmio_read(uintptr_t addr, uint8_t size) {
        uint64_t v = 0;
        if(size == 8)
            v = *(volatile uint64_t*)(addr + phys_mem_map);
        else if(size == 4)
            v = *(volatile uint32_t*)(addr + phys_mem_map);
        else if(size == 2)
            v = *(volatile uint16_t*)(addr + phys_mem_map);
        else if(size == 1)
            v = *(volatile uint8_t*)(addr + phys_mem_map);
        else
            PANIC("Unknown size");

        format::format_to(net::luna_debug::Writer{}, "r {:#x} {:#x} {}", addr, v, (uint16_t)size);

        return v;
    }

    private:
    uintptr_t base;
    size_t len;

    vm::Vm* vm;
};*/

struct RealModeCpu {
    struct Regs {
        uint32_t eax, ebx, ecx, edx, esi, edi;
        uint32_t eflags;
    };

    RealModeCpu(): cpu{1} {
        cpu.cpus[0].set(vm::VmCap::FullPIOAccess, true);

        for(size_t i = 0; i < 0xFFFF'FFFF; i += pmm::block_size)
            cpu.mm->map(i, i, paging::mapPagePresent | paging::mapPageWrite | paging::mapPageExecute);

        auto pa = pmm::alloc_n_blocks(8);

        payload = (uint8_t*)(pa + phys_mem_map + 0x2C00);
        data = (uint8_t*)(pa + phys_mem_map + 0x2D00);
        data_addr = 0x7D00;

        for(size_t i = 0; i < 8; i++)
            cpu.mm->map(pa + i * pmm::block_size, 0x5000 + i * pmm::block_size, paging::mapPagePresent | paging::mapPageWrite | paging::mapPageExecute);
    }

    void intn(uint8_t n, Regs& regs) {
        uint8_t code[] = {
            0xCD, n, // int n
            0x0F, 0x01, 0xD9 // vmmcall
        };

        memcpy(payload, code, sizeof(code));

        vm::RegisterState state{};
        cpu.cpus[0].get_regs(state);

        state.rax = regs.eax;
        state.rbx = regs.ebx;
        state.rcx = regs.ecx;
        state.rdx = regs.edx;
        state.rsi = regs.esi;
        state.rdi = regs.edi;

        state.rflags = regs.eflags;

        state.cs.selector = 0;
        state.cs.base = 0;
        state.rip = 0x7C00;
        state.rsp = 0x7C00;
        cpu.cpus[0].set_regs(state);

        if(!cpu.cpus[0].run())
            PANIC("vbe: Failed to run BIOS instruction");

        cpu.cpus[0].get_regs(state);
        regs.eax = state.rax;
        regs.ebx = state.rbx;
        regs.ecx = state.rcx;
        regs.edx = state.rdx;
        regs.esi = state.rsi;
        regs.edi = state.rdi;

        regs.eflags = state.rflags;
    }

    uint8_t* payload, *data;
    uint32_t data_addr;
    vm::Vm cpu;
};

bool check_vbe_error(uint16_t ax) {
    if((ax & 0xFF) != 0x4F)
        return false; // Call Unsupported

    if((ax >> 8) != 0)
        return false; // Errorred
    
    return true;
}

static std::vector<std::pair<uint16_t, vbe::VBEModeInfoBlock>> mode_list{};
static std::lazy_initializer<RealModeCpu> vcpu;
void vbe::init() {
    vcpu.init();

    RealModeCpu::Regs regs{};
    regs.eflags = (1 << 1);

    auto* info = (VBEInfoBlock*)vcpu->data;
    memcpy(info->signature, "VBE2", 4);

    regs.eax = 0x4F00;
    regs.edi = vcpu->data_addr;
    vcpu->intn(0x10, regs);
    ASSERT(check_vbe_error(regs.eax));

    print("vbe: Detected VBE {}.{}, VRAM: {} KiB\n", (uint16_t)info->major_ver, (uint16_t)info->minor_ver, info->total_mem * 64);

    auto far_ptr_to_va = [&](const VBEFarPtr& p) {
        auto phys = p.phys();

        if(phys >= vcpu->data_addr && phys <= (vcpu->data_addr + sizeof(VBEInfoBlock)))
            return (uintptr_t)(vcpu->data + (phys - vcpu->data_addr));
        else
            return (uintptr_t)(phys + phys_mem_map);
    };

    auto print_str = [&](const char* prefix, const VBEFarPtr& p) {
        if(!p.phys())
            return;
        
        const auto* str = (const char*)far_ptr_to_va(p);
        print("{}{}\n", prefix, str);
    };
    print_str("     OEM: ", info->oem);
    print_str("     Vendor: ", info->vendor);
    print_str("     Product: ", info->product_name);
    print_str("     Product Revision: ", info->product_rev);

    auto* modes = (uint16_t*)far_ptr_to_va(info->mode_info);
    for(; *modes != 0xFFFF; modes++)
        mode_list.push_back({*modes, {}});

    for(auto& mode : mode_list) {
        regs.eax = 0x4F01;
        regs.ecx = mode.first;
        regs.edi = vcpu->data_addr;

        vcpu->intn(0x10, regs);
        ASSERT(check_vbe_error(regs.eax));

        mode.second = *(VBEModeInfoBlock*)vcpu->data;
    }    
}

gpu::Mode vbe::set_mode(std::pair<uint16_t, uint16_t> res, uint8_t bpp) {
    uint16_t mode_num = -1;
    gpu::Mode ret{};
    const auto [x_res, y_res] = res;
    for(const auto& [num, mode] : mode_list) {
        if(mode.width == x_res && mode.height == y_res && mode.bpp == bpp) {
            mode_num = num;
            
            ret = {.width = mode.width, .height = mode.height, .pitch = mode.pitch, .bpp = mode.bpp};
            break;
        }
    }

    ASSERT(mode_num != -1);

    RealModeCpu::Regs regs{};
    regs.eflags = (1 << 1);
    regs.eax = 0x4F02;
    regs.ebx = mode_num | (1 << 14); // Mode + LFB

    vcpu->intn(0x10, regs);
    ASSERT(check_vbe_error(regs.eax));

    return ret;
}

