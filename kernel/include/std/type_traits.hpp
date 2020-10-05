#pragma once

#include <std/bits/traits.hpp>
#include <std/bits/declval.hpp>

namespace std
{
    namespace detail
    {
        template<typename>
        using true_type_for = std::true_type;

        template<typename T>
        auto test_returnable(int) -> true_type_for<T()>;

        template<typename>
        auto test_returnable(...) -> std::false_type;

        template<typename From, typename To>
        auto test_nonvoid_convertible(int) -> true_type_for<decltype(std::declval<void(&)(To)>()(std::declval<From>()))>;

        template<typename, typename>
        auto test_nonvoid_convertible(...) -> std::false_type;
    } // namespace detail
    
    template<typename From, typename To>
    struct is_convertible : std::integral_constant<bool, (decltype(detail::test_returnable<To>(0))::value && decltype(detail::test_nonvoid_convertible<From, To>(0))::value) || (std::is_void<From>::value && std::is_void<To>::value)> {};

    template<typename From, typename To>
    inline constexpr bool is_convertible_v = is_convertible<From, To>::value;

    template<size_t L, size_t A>
    struct aligned_storage {
        struct type {
            alignas(A) unsigned char data[L];
        };
    };

    template<size_t L, size_t A = alignof(int)>
    using aligned_storage_t = typename aligned_storage<L, A>::type;
} // namespace std
