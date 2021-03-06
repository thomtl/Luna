#include <Luna/vmm/emulate.hpp>

#include <Luna/misc/log.hpp>

constexpr uint64_t get_mask(uint8_t s) {
    switch (s) {
        case 1: return 0xFF;
        case 2: return 0xFFFF;
        case 4: return 0xFFFF'FFFF;
        case 8: return 0xFFFF'FFFF'FFFF'FFFF;
        default: PANIC("Unknown size");
    }
}

uint64_t& get_r64(vm::RegisterState& regs, vm::emulate::r64 r) {
    using namespace vm::emulate;
    switch (r) {
        case r64::Rax: return regs.rax;
        case r64::Rcx: return regs.rcx;
        case r64::Rdx: return regs.rdx;
        case r64::Rbx: return regs.rbx;
        case r64::Rsp: return regs.rsp;
        case r64::Rbp: return regs.rbp;
        case r64::Rsi: return regs.rsi;
        case r64::Rdi: return regs.rdi;
        default: PANIC("Unknown reg");
    }
}

vm::RegisterState::Segment& get_sreg(vm::RegisterState& regs, vm::emulate::sreg r) {
    using namespace vm::emulate;
    switch (r) {
        case sreg::Es: return regs.es;
        case sreg::Cs: return regs.cs;
        case sreg::Ss: return regs.ss;
        case sreg::Ds: return regs.ds;
        case sreg::Fs: return regs.fs;
        case sreg::Gs: return regs.gs;
        default: PANIC("Unknown reg");
    }
}



uint64_t vm::emulate::read_r64(vm::RegisterState& regs, vm::emulate::r64 r, uint8_t s) {
    return get_r64(regs, r) & get_mask(s);
}

void vm::emulate::write_r64(vm::RegisterState& regs, vm::emulate::r64 r, uint64_t v, uint8_t s) {
    auto* reg = &get_r64(regs, r);

    switch (s) {
        case 1: *(uint8_t*)reg = (uint8_t)v; break;
        case 2: *(uint16_t*)reg = (uint16_t)v; break;
        case 4: *reg = (uint32_t)v; break; // Zero-extend in 64bit mode
        case 8: *reg = v; break;
        default: PANIC("Unknown size");
    }
}

struct Modrm {
    uint8_t mod, reg, rm;
};

Modrm parse_modrm(uint8_t v) {
    return Modrm{.mod = (uint8_t)((v >> 6) & 0b11), .reg = (uint8_t)((v >> 3) & 0b111), .rm = (uint8_t)(v & 0b111)};
}



void vm::emulate::emulate_instruction(vm::VCPU* vcpu, std::pair<uintptr_t, size_t> mmio_region, uint8_t instruction[max_x86_instruction_size], vm::RegisterState& regs, vm::AbstractMMIODriver* driver) {
    ASSERT(!(regs.efer & (1 << 10)));
    ASSERT(!(regs.cr0 & (1 << 31)));
    uint8_t default_operand_size = regs.cs.attrib.db ? 4 : 2;
    uint8_t other_operand_size = regs.cs.attrib.db ? 2 : 4;
    uint8_t address_size = default_operand_size, operand_size = default_operand_size;
    auto* segment = &get_sreg(regs, sreg::Ds);
    bool rep = false;

    uint8_t i = 0;
    bool done = false;

    auto read16 = [&]() -> uint16_t { auto low = instruction[++i]; auto high = instruction[++i]; return (low | (high << 8)); };
    auto read32 = [&]() -> uint32_t { auto low = read16(); auto high = read16(); return (low | (high << 16)); };
    auto readN = [&](uint8_t size) -> uintptr_t { 
        if(size == 2)
            return read16();
        else if(size == 4)
            return read32();
        else
            PANIC("Unknown read size");
    };

    while(!done) {
        auto op = instruction[i];

        switch (op) {
        case 0x26: segment = &get_sreg(regs, sreg::Es); break; // ES segment override
        case 0x2E: segment = &get_sreg(regs, sreg::Cs); break; // CS segment override
        case 0x36: segment = &get_sreg(regs, sreg::Ss); break; // SS segment override
        case 0x3E: segment = &get_sreg(regs, sreg::Ds); break; // DS segment override
        case 0x64: segment = &get_sreg(regs, sreg::Fs); break; // FS segment override
        case 0x65: segment = &get_sreg(regs, sreg::Gs); break; // GS segment override

        case 0x66: operand_size = other_operand_size; break; // Operand Size Override
        case 0x67: address_size = other_operand_size; break; // Address Size Override
        
        case 0xF3: rep = true; break; // REP Prefix
        
        case 0x88: { // MOV r/m8, r8
            auto mod = parse_modrm(instruction[++i]);
            
            if(mod.mod == 0) {
                if(mod.rm == 0b100 || mod.rm == 0b101) {
                    PANIC("TODO");
                } else {
                    auto src = read_r64(regs, (vm::emulate::r64)mod.rm, address_size);
                    auto v = read_r64(regs, (vm::emulate::r64)mod.reg, 1);

                    driver->mmio_write(segment->base + src, v, 1);
                }
            } else {
                print("Unknown MODR/M: {:#x}\n", (uint16_t)mod.mod);
                PANIC("Unknown");
            }

            done = true;
            break;
        }

        case 0x89: { // MOV r/m{16, 32}, r{16, 32}
            auto mod = parse_modrm(instruction[++i]);
            
            if(mod.mod == 0) {
                if(mod.rm == 0b100 || mod.rm == 0b101) {
                    PANIC("TODO");
                } else {
                    auto src = read_r64(regs, (vm::emulate::r64)mod.rm, address_size);
                    auto v = read_r64(regs, (vm::emulate::r64)mod.reg, operand_size);

                    driver->mmio_write(segment->base + src, v, operand_size);
                }
            } else if(mod.mod == 1) {
                if(mod.rm == 0b100 || mod.rm == 0b101)
                    PANIC("TODO");
                else {
                    auto src = read_r64(regs, (vm::emulate::r64)mod.rm, address_size);
                    src += instruction[++i];
                    auto v = read_r64(regs, (vm::emulate::r64)mod.reg, operand_size);

                    driver->mmio_write(segment->base + src, v, operand_size);
                }
            } else {
                print("Unknown MODR/M: {:#x}\n", (uint16_t)mod.mod);
                PANIC("Unknown");
            }

            done = true;
            break;
        }

        case 0x8A: { // MOV r8, r/m8
            auto mod = parse_modrm(instruction[++i]);
            
            if(mod.mod == 0) {
                if(mod.rm == 0b100 || mod.rm == 0b101) {
                    PANIC("TODO");
                } else {
                    auto src = read_r64(regs, (vm::emulate::r64)mod.rm, address_size);
                    auto v = driver->mmio_read(segment->base + src, 1);
                    write_r64(regs, (vm::emulate::r64)mod.reg, v, 1);
                }
            } else {
                print("Unknown MODR/M: {:#x}\n", (uint16_t)mod.mod);
                PANIC("Unknown");
            }

            done = true;
            break;
        }

        case 0x8B: { // MOV r{16, 32}, r/m{16, 32}
            auto mod = parse_modrm(instruction[++i]);
            
            if(mod.mod == 0) {
                if(mod.rm == 0b100)
                    PANIC("TODO");
                else if(mod.rm == 0b101) {
                    auto src = readN(address_size);
                    auto v = driver->mmio_read(segment->base + src, operand_size);
                    write_r64(regs, (vm::emulate::r64)mod.reg, v, operand_size);
                } else {
                    auto src = read_r64(regs, (vm::emulate::r64)mod.rm, address_size);
                    auto v = driver->mmio_read(segment->base + src, operand_size);
                    write_r64(regs, (vm::emulate::r64)mod.reg, v, operand_size);
                }
            } else if(mod.mod == 1) {
                if(mod.rm == 0b100)
                    PANIC("TODO");
                else {
                    auto src = read_r64(regs, (vm::emulate::r64)mod.rm, address_size);
                    src += instruction[++i];
                    auto v = driver->mmio_read(segment->base + src, operand_size);
                    write_r64(regs, (vm::emulate::r64)mod.reg, v, operand_size);
                }
            } else if(mod.mod == 2) {
                if(mod.rm == 0b100)
                    PANIC("TODO");
                else {
                    auto src = read_r64(regs, (vm::emulate::r64)mod.rm, address_size);
                    src += read32();
                    auto v = driver->mmio_read(segment->base + src, operand_size);
                    write_r64(regs, (vm::emulate::r64)mod.reg, v, operand_size);
                }

            } else {
                print("Unknown MODR/M: {:#x}\n", (uint16_t)mod.mod);
                PANIC("Unknown");
            }

            done = true;
            break;
        }

        case 0xA1: { // MOV {AX, EAX}, moffs{16, 32}
            auto src = readN(address_size);
            auto v = driver->mmio_read(segment->base + src, operand_size);
            write_r64(regs, vm::emulate::r64::Rax, v, operand_size);
            done = true;
            break;
        }

        case 0xA3: { // MOV moffs{16, 32}, {AX, EAX}
            auto dst = readN(address_size);
            auto v = read_r64(regs, vm::emulate::r64::Rax, operand_size);
            driver->mmio_write(segment->base + dst, v, operand_size);
            done = true;
            break;
        }

        case 0xA5: { // MOVS
            auto do_mov = [&]() {
                auto src = read_r64(regs, vm::emulate::r64::Rsi, address_size);
                auto dst = read_r64(regs, vm::emulate::r64::Rdi, address_size);

                uint64_t v = 0;
                if(ranges_overlap(segment->base + src, operand_size, mmio_region.first, mmio_region.second))
                    v = driver->mmio_read(segment->base + src, operand_size);
                else
                    vcpu->dma_read(segment->base + src, {(uint8_t*)&v, operand_size});
            
                if(ranges_overlap(regs.es.base + dst, operand_size, mmio_region.first, mmio_region.second))
                    driver->mmio_write(regs.es.base + dst, v, operand_size);
                else
                    vcpu->dma_write(regs.es.base + dst, {(uint8_t*)&v, operand_size});

                write_r64(regs, vm::emulate::r64::Rsi, src + operand_size, address_size);
                write_r64(regs, vm::emulate::r64::Rdi, dst + operand_size, address_size);
            };

            if(!rep) {
                do_mov();
            } else {
                size_t count = read_r64(regs, vm::emulate::r64::Rcx, address_size);
                bool dir = (regs.rflags >> 10) & 1;

                if(!dir) {
                    for(size_t i = 0; i < count; i++)
                        do_mov();

                    regs.rcx = 0;
                } else {
                    PANIC("TODO");
                }
            }

            done = true;
            break;
        }

        case 0xC6: { // MOV r/m8, imm8
            auto mod = parse_modrm(instruction[++i]);

            if(mod.mod == 0) {
                PANIC("TODO");
            } else if(mod.mod == 0b01) {
                PANIC("TODO");
            } else if(mod.mod == 0b10) {
                auto dst = read_r64(regs, (vm::emulate::r64)mod.reg, address_size);
                dst += read32();

                auto v = instruction[++i];
                driver->mmio_write(segment->base + dst, v, 1);
            } else {
                PANIC("TODO");
            }

            done = true;
            break;
        }

        case 0xC7: { // MOV r/m{16, 32}, imm{16, 32}
            auto mod = parse_modrm(instruction[++i]);

            if(mod.mod == 0) {
                if(mod.rm == 0b100) {
                    PANIC("TODO");
                } else if(mod.rm == 0b101) {
                    auto src = read32();

                    auto v = readN(operand_size);

                    driver->mmio_write(segment->base + src, v, operand_size);
                } else {
                    auto v = readN(operand_size);
                    auto src = read_r64(regs, (vm::emulate::r64)mod.rm, address_size);

                    driver->mmio_write(segment->base + src, v, operand_size);
                }
            } else if(mod.mod == 1) {
                if(mod.rm == 0b100 || mod.rm == 0b101)
                    PANIC("TODO");
                else {
                    auto src = read_r64(regs, (vm::emulate::r64)mod.rm, address_size);
                    src += instruction[++i];

                    auto v = readN(operand_size);

                    driver->mmio_write(segment->base + src, v, operand_size);
                }
            } else {
                print("Unknown MODR/M: {:#x}\n", (uint16_t)mod.mod);
                PANIC("Unknown");
            }

            done = true;
            break;
        }

        
        default:
            print("vm: Unknown instruction byte: ");
            for(size_t j = 0; j < max_x86_instruction_size; j++) {
                if(i == j)
                    print("[{:x}] ", (uint16_t)instruction[j]);
                else
                    print("{:x} ", (uint16_t)instruction[j]);
            }
            print("\n");

            PANIC("Unknown instruction");
            break;
        }
        i++;
    }

    regs.rip += i;
}