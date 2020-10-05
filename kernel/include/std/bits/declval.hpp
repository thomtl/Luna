#pragma once

#include <std/bits/traits.hpp>

namespace std
{
    template<typename T>
    typename std::add_rvalue_reference<T>::type declval() noexcept;
} // namespace std
