#include <Luna/cpu/smp.hpp>
#include <Luna/misc/log.hpp>

void smp::start_cpus(stivale2::Parser& boot_info, void (*f)(stivale2_smp_info*)) {
    auto* smp = (stivale2_struct_tag_smp*)boot_info.get_tag(STIVALE2_STRUCT_TAG_SMP_ID);
    bool x2apic = (smp->flags & 0x1);

    auto cpu_count = smp->cpu_count;
    print("smp: Detected {} CPUs, with {:s}\n", cpu_count, x2apic ? "x2APIC" : "xAPIC");

    for(size_t i = 0; i < cpu_count; i++) {
        auto& cpu = smp->smp_info[i];
        auto is_bsp = (cpu.lapic_id == smp->bsp_lapic_id);

        auto lapic_id = cpu.lapic_id;
        auto acpi_uid = cpu.processor_id;
        print("   - CPU {}: APIC UID: {} {}\n", lapic_id, acpi_uid, is_bsp ? "is BSP" : "is AP");

        if(!is_bsp) {
            auto stack_base = pmm::alloc_block() + phys_mem_map;
            auto stack_top = stack_base + 0x1000;

            cpu.target_stack = stack_top;
            
            __atomic_store_n(&cpu.goto_address, (uintptr_t)f, __ATOMIC_SEQ_CST);
        }   
    }
}