#pragma once

#include <std/type_traits.hpp>

namespace std
{
    template<typename T>
    constexpr T* to_address(T* p) {
        static_assert(!std::is_function_v<T>);
        return p;
    }

    template<typename T> struct pointer_traits;
    template<typename T> struct pointer_traits<T*>;

    template<typename T>
    constexpr auto to_address(const T& p) {
        if constexpr (requires{ std::pointer_traits<T>::to_address(p); }) {
            return std::pointer_traits<T>::to_address(p);
        } else {
            return std::to_address(p.operator->());
        }
    }
} // namespace std
