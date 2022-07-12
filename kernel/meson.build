project('Luna', 'cpp', version: '0.1.0')

main_sources = files(
    'source/cpp_support.cpp',
    'source/main.cpp',

    'source/cpu/amd/asid.cpp',
    'source/cpu/amd/npt.cpp',
    'source/cpu/amd/svm.cpp',

    'source/cpu/intel/ept.cpp',
    'source/cpu/intel/vmx.cpp',

    'source/cpu/cpu.cpp',
    'source/cpu/idt.cpp',
    'source/cpu/lapic.cpp',
    'source/cpu/paging.cpp',
    'source/cpu/pio.cpp',
    'source/cpu/regs.cpp',
    'source/cpu/smp.cpp',
    'source/cpu/threads.cpp',
    'source/cpu/tsc.cpp',

    'source/drivers/gpu/intel/gpu.cpp',
    'source/drivers/gpu/intel/lil_imports.cpp',

    'source/drivers/gpu/lfb/vbe.cpp',
    'source/drivers/gpu/lfb/lfb.cpp',

    'source/drivers/gpu/gpu.cpp',
    'source/drivers/gpu/tty.cpp',

    'source/drivers/iommu/amd/amd_vi.cpp',
    'source/drivers/iommu/amd/io_paging.cpp',

    'source/drivers/iommu/intel/sl_paging.cpp',
    'source/drivers/iommu/intel/vt_d.cpp',

    'source/drivers/iommu/iommu.cpp',

    'source/drivers/net/realtek/rtl81x9.cpp',
    'source/drivers/sound/hda.cpp',

    'source/drivers/storage/ahci.cpp',
    'source/drivers/storage/ata.cpp',
    'source/drivers/storage/scsi.cpp',
    'source/drivers/storage/usb_msd.cpp',

    'source/drivers/timers/hpet.cpp',

    'source/drivers/usb/ehci/ehci.cpp',
    'source/drivers/usb/hid/boot_mouse.cpp',
    'source/drivers/usb/hid/boot_keyboard.cpp',
    'source/drivers/usb/serial/ch341.cpp',
    'source/drivers/usb/xhci/xhci.cpp',
    'source/drivers/usb/usb.cpp',

    'source/drivers/acpi.cpp',
    'source/drivers/ioapic.cpp',
    'source/drivers/laihost.cpp',
    'source/drivers/pci.cpp',
    'source/drivers/vga.cpp',

    'source/fs/echfs.cpp',
    'source/fs/fs.cpp',
    'source/fs/mbr.cpp',
    'source/fs/storage_dev.cpp',
    'source/fs/vfs.cpp',

    'source/gui/bmp_parser.cpp',
    'source/gui/framework.cpp',
    'source/gui/gui.cpp',

    'source/mm/pmm.cpp',
    'source/mm/vmm.cpp',
    'source/mm/hmm.cpp',

    'source/net/if.cpp',
    'source/net/ipv4.cpp',
    'source/net/udp.cpp',

    'source/vmm/drivers/gpu/edid.cpp',
    'source/vmm/drivers/nvme.cpp',
    
    'source/vmm/emulate.cpp',
    'source/vmm/vm.cpp',

    'source/misc/debug.cpp',
    'source/misc/log.cpp',
    'source/misc/font.cpp',
    'source/misc/misc.cpp',
    'source/misc/stivale2.cpp',
    'source/misc/ubsan.cpp',

    'source/std/string.cpp')

cpp_includes = include_directories('include')

nasm_sources = files(
    'source/cpu/amd/svm_low.asm',
    'source/cpu/intel/vmx_low.asm',
    'source/cpu/irqs.asm',
    'source/cpu/threading.asm')

nasm = find_program('nasm')
nasm_gen = generator(nasm, output: '@BASENAME@.o', arguments: ['-f', 'elf64', '-g', '-F', 'dwarf', '@INPUT@', '-o', '@OUTPUT@'])
nasm_objects = nasm_gen.process(nasm_sources)

extra_warning_flags = ['-Wreorder', '-Wdeprecated-copy-dtor', '-Wno-unknown-pragmas', '-Wduplicated-cond', '-Wduplicated-branches',
                       '-Wlogical-op', '-Wno-non-virtual-dtor', '-Werror', '-Wno-address', '-Wno-volatile', '-Wno-address-of-packed-member']

flags_c_common = ['-m64', '-march=x86-64', '-fno-PIC', '-Wall', '-Wextra', '-ffreestanding', '-mno-red-zone', '-fstrict-volatile-bitfields',
                  '-nostdlib', '-mcmodel=kernel', '-fno-omit-frame-pointer', '-g', '-O2',
                  '-mno-mmx', '-mno-sse', '-mno-sse2', '-mno-sse3', '-mno-sse4', '-mno-sse4.1', '-mno-sse4.2', '-mno-sse4a',
                  '-fsanitize=undefined', '-fstack-protector-all']

cpp_flags = []
cpp_flags += flags_c_common
cpp_flags += extra_warning_flags

cpp_flags += ['-std=c++20', '-fno-exceptions', '-fno-rtti', '-fuse-cxa-atexit', '-fconcepts-diagnostics-depth=2']

c_flags = []
c_flags += flags_c_common
c_flags += ['-std=c18']

ld_script = meson.current_source_dir() + '/build/linker.ld'

ld_flags = ['-T', ld_script, '-nostdlib', '-mcmodel=kernel', '-fno-PIC', '-no-pie', '-Wl,--build-id=none', 
            '-Wl,-z,max-page-size=0x1000,-n']

add_global_arguments(c_flags, language: 'c')
add_global_link_arguments(ld_flags, language: 'c')
add_global_arguments(cpp_flags, language: 'cpp')
add_global_link_arguments(ld_flags, language: 'cpp')


lai = subproject('lai')
lai_dep = lai.get_variable('dependency')

lil = subproject('lil')
lil_dep = lil.get_variable('dependency')

deps = [lai_dep, lil_dep]

executable('luna.bin', main_sources, nasm_objects, dependencies: deps, include_directories: cpp_includes, link_depends: ld_script)
