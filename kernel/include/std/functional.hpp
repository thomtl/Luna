#pragma once

#include <Luna/common.hpp>
#include <std/bits/move.hpp>

namespace std
{
    template<typename Key>
    struct hash;

    template<>
    struct hash<uint8_t> {
        size_t operator()(const uint8_t v) const {
            return v;
        }
    };

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
        size_t operator()(const uint64_t v) const {
            return v;
        }
    };

    template<>
    struct hash<int64_t> {
        size_t operator()(const int64_t v) const {
            return (size_t)v;
        }
    };

    template<typename T = void>
    struct less;

    template<typename T>
    struct less {
        constexpr bool operator()(const T& lhs, const T& rhs) const {
            return lhs < rhs;
        }
    };

    template<>
    struct less<void> {
        template<typename T, typename U>
        constexpr auto operator()(T&& lhs, U&& rhs) const noexcept(noexcept(std::forward<T>(lhs) < std::forward<U>(rhs))) 
                    -> decltype(std::forward<T>(lhs) < std::forward<U>(rhs)) {
            return std::forward<T>(lhs) < std::forward<U>(rhs);
        }
    };
} // namespace std
