#pragma once

#include <Luna/common.hpp>
#include <Luna/3rdparty/stivale2.h>

#include <std/utility.hpp>

namespace stivale2
{
    struct Parser {
        Parser(const stivale2_struct* info);

        void* acpi_rsdp() const;
        std::pair<const stivale2_mmap_entry*, size_t> mmap() const;

        private:
        const stivale2_struct* info;

        const stivale2_tag* get_tag(uint64_t id) const;
    };
} // namespace stivale2
