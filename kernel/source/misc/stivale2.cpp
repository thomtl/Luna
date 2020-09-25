#include <Luna/misc/stivale2.hpp>

stivale2::Parser::Parser(const stivale2_struct* info): info{info} {

}

void* stivale2::Parser::acpi_rsdp() const {
    const auto* tag = (const stivale2_struct_tag_rsdp*)get_tag(STIVALE2_STRUCT_TAG_RSDP_ID);
    if(tag)
        return (void*)tag->rsdp;

    return nullptr;
}

std::pair<const stivale2_mmap_entry*, size_t> stivale2::Parser::mmap() const {
    const auto* tag = (const stivale2_struct_tag_memmap*)get_tag(STIVALE2_STRUCT_TAG_MEMMAP_ID);

    if(tag)
        return {tag->memmap, tag->entries};
    
    return {nullptr, 0};
}

const stivale2_tag* stivale2::Parser::get_tag(uint64_t id) const {
    const auto* tag = (const stivale2_tag*)info->tags;
    while(tag) {
        if(tag->identifier == id)
            return tag;

        tag = (stivale2_tag*)tag->next;
    }

    return nullptr;
}