#pragma once

#include <Luna/common.hpp>
#include <Luna/3rdparty/stivale2.h>

#include <std/utility.hpp>
#include <std/span.hpp>

namespace stivale2
{
    const char* mmap_type_to_string(uint32_t type);

    struct Parser {
        Parser(const stivale2_struct* info);

        uintptr_t acpi_rsdp() const;
        const std::span<stivale2_mmap_entry>& mmap() const;
        std::span<stivale2_mmap_entry>& mmap();

        private:
        const stivale2_struct* _info;
        std::span<stivale2_mmap_entry> _mmap;
        uintptr_t _rsdp;

        const stivale2_tag* get_tag(uint64_t id) const;
    };
} // namespace stivale2
