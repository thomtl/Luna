project('Luna', 'cpp', version: '0.1.0')

main_sources = files(
    'source/cpp_support.cpp',
    'source/main.cpp',

    'source/cpu/cpu.cpp',
    'source/cpu/idt.cpp',
    'source/cpu/lapic.cpp',
    'source/cpu/paging.cpp',
    'source/cpu/pio.cpp',
    'source/cpu/regs.cpp',
    'source/cpu/smp.cpp',

    'source/drivers/acpi.cpp',
    'source/drivers/ioapic.cpp',
    'source/drivers/laihost.cpp',
    'source/drivers/pci.cpp',

    'source/mm/pmm.cpp',
    'source/mm/vmm.cpp',
    'source/mm/hmm.cpp',

    'source/misc/misc.cpp',
    'source/misc/stivale2.cpp',

    'source/std/string.cpp')

cpp_includes = include_directories('include')

nasm_sources = files(
    'source/cpu/irqs.asm')

nasm = find_program('nasm')
nasm_gen = generator(nasm, output: '@BASENAME@.o', arguments: ['-f', 'elf64', '-g', '-F', 'dwarf', '@INPUT@', '-o', '@OUTPUT@'])
nasm_objects = nasm_gen.process(nasm_sources)

extra_warning_flags = ['-Wreorder', '-Wdeprecated-copy-dtor', '-Wno-unknown-pragmas', '-Wduplicated-cond', '-Wduplicated-branches',
                       '-Wlogical-op', '-Wno-non-virtual-dtor', '-Werror', '-Wno-address']

flags_c_common = ['-m64', '-march=x86-64', '-fno-PIC', '-Wall', '-Wextra', '-ffreestanding', '-mno-red-zone', 
                  '-nostdlib', '-mcmodel=kernel', '-fno-omit-frame-pointer', '-g', '-O2',
                  '-mno-mmx', '-mno-sse', '-mno-sse2', '-mno-sse3', '-mno-sse4', '-mno-sse4.1', '-mno-sse4.2', '-mno-sse4a']

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

deps = [lai_dep]

executable('luna.bin', main_sources, nasm_objects, dependencies: deps, include_directories: cpp_includes, link_depends: ld_script)
