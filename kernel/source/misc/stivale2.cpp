#include <Luna/misc/stivale2.hpp>

const char* stivale2::mmap_type_to_string(uint32_t type){
    switch (type) {
        case STIVALE2_MMAP_USABLE: return "Usable";
        case STIVALE2_MMAP_RESERVED: return "Reserved";
        case STIVALE2_MMAP_ACPI_RECLAIMABLE: return "ACPI Reclaimable";
        case STIVALE2_MMAP_ACPI_NVS: return "ACPI Non-Volatile Storage";
        case STIVALE2_MMAP_BAD_MEMORY: return "Bad Memory";
        case STIVALE2_MMAP_BOOTLOADER_RECLAIMABLE: return "Bootloader Reclaimable";
        case STIVALE2_MMAP_KERNEL_AND_MODULES: return "Kernel";
        default: return "Unknown";
    }
}

stivale2::Parser::Parser(const stivale2_struct* info): _info{info} {
    {
        auto* tag = (stivale2_struct_tag_memmap*)get_tag(STIVALE2_STRUCT_TAG_MEMMAP_ID);
        ASSERT(tag);

        _mmap = std::span<stivale2_mmap_entry>{&(tag->memmap[0]), tag->entries};
    }

    {
        auto* tag = (stivale2_struct_tag_rsdp*)get_tag(STIVALE2_STRUCT_TAG_RSDP_ID);
        ASSERT(tag);

        _rsdp = tag->rsdp;
    }
}

uintptr_t stivale2::Parser::acpi_rsdp() const {
    return _rsdp;
}

const std::span<stivale2_mmap_entry>& stivale2::Parser::mmap() const {
    return _mmap;
}

std::span<stivale2_mmap_entry>& stivale2::Parser::mmap() {
    return _mmap;
}

const stivale2_tag* stivale2::Parser::get_tag(uint64_t id) const {
    const auto* tag = (const stivale2_tag*)_info->tags;
    while(tag) {
        if(tag->identifier == id)
            return tag;

        tag = (stivale2_tag*)(tag->next + phys_mem_map);
    }

    return nullptr;
}