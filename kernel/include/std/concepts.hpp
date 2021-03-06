#pragma once

#include <std/type_traits.hpp>

namespace std {    
    template<typename T, typename U>
    concept same_as = std::is_same_v<T, U> && std::is_same_v<U, T>;

    template<typename From, typename To>
    concept convertible_to = std::is_convertible_v<From, To> && requires(std::add_rvalue_reference_t<From> (&f)()) { static_cast<To>(f()); };
} // namespace std
