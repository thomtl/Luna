#pragma once

#include <Luna/common.hpp>

namespace std
{
    template<typename Key>
    struct hash;

    template<>
    struct hash<uint16_t> {
        size_t operator()(const uint16_t v) const {
            return v;
        }
    };

    template<>
    struct hash<uint32_t> {
        size_t operator()(const uint32_t v) const {
            return v;
        }
    };

    template<>
    struct hash<uint64_t> {
        size_t operator()(const uint32_t v) const {
            return v;
        }
    };
} // namespace std
