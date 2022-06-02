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

    template< class T >
    struct decay {
    private:
        typedef typename std::remove_reference<T>::type U;
    public:
        typedef typename std::conditional< 
            std::is_array<U>::value,
            typename std::remove_extent<U>::type*,
            typename std::conditional< 
                std::is_function<U>::value,
                typename std::add_pointer<U>::type,
                typename std::remove_cv<U>::type
            >::type
        >::type type;
    };
 
    template <class...>
    struct common_type {};

    template <class T>
    struct common_type<T> : common_type<T, T> {};

    namespace detail {
        template <class T1, class T2>
        using cond_t = decltype(false ? std::declval<T1>() : std::declval<T2>());
 
        template <class T1, class T2, class=void>
        struct common_type_2_impl {};
 
        template <class T1, class T2>
        struct common_type_2_impl<T1, T2, void_t<cond_t<T1, T2>>> {
            using type = typename std::decay<cond_t<T1, T2>>::type;
        };

        template <class AlwaysVoid, class T1, class T2, class...R>
        struct common_type_multi_impl {};
 
        template <class T1, class T2, class...R>
        struct common_type_multi_impl<void_t<typename common_type<T1, T2>::type>, T1, T2, R...> : common_type<typename common_type<T1, T2>::type, R...> {};
    } // namespace detail

    template <class T1, class T2>
    struct common_type<T1, T2> : detail::common_type_2_impl<typename std::decay<T1>::type, typename std::decay<T2>::type> {};
 
    template <class T1, class T2, class... R>
    struct common_type<T1, T2, R...>
        : detail::common_type_multi_impl<void, T1, T2, R...> {};
    
    template< class... T >
    using common_type_t = typename common_type<T...>::type;
} // namespace std
