#pragma once

#include <stddef.h>

namespace std
{
    template<typename...>
    using void_t = void;

    // Taken from https://en.cppreference.com/w/cpp/types/integral_constant
    template<typename T, T v>
    struct integral_constant {
        static constexpr T value = v;
        using value_type = T;
        using type = integral_constant;
        constexpr operator value_type() const noexcept { return value; }
        constexpr value_type operator()() const noexcept { return value; }
    };

    using true_type = integral_constant<bool, true>;
    using false_type = integral_constant<bool, false>;

    // Taken from https://en.cppreference.com/w/cpp/types/remove_reference
    template<typename T> struct remove_reference { typedef T type; };
    template<typename T> struct remove_reference<T&> { typedef T type; };
    template<typename T> struct remove_reference<T&&> { typedef T type; };

    template<typename T>
    using remove_reference_t = typename remove_reference<T>::type;

    // Taken from https://en.cppreference.com/w/cpp/types/remove_cv
    template<typename T> struct remove_cv { typedef T type; };
    template<typename T> struct remove_cv<const T> { typedef T type; };
    template<typename T> struct remove_cv<volatile T> { typedef T type; };
    template<typename T> struct remove_cv<const volatile T> { typedef T type; };

    template<typename T>
    using remove_cv_t = typename remove_cv<T>::type;

    // Taken from https://en.cppreference.com/w/cpp/types/is_same
    template<typename T, typename U> struct is_same : std::false_type {};
    template<typename T> struct is_same<T, T> : std::true_type {};

    template<typename T, typename U>
    inline constexpr bool is_same_v = is_same<T, U>::value;

    template<typename T> struct is_integral : std::integral_constant<bool, std::is_same_v<std::remove_cv_t<T>, bool> || 
                                                std::is_same_v<std::remove_cv_t<T>, char> || std::is_same_v<std::remove_cv_t<T>, signed char> || std::is_same_v<std::remove_cv_t<T>, unsigned char> ||
                                                std::is_same_v<std::remove_cv_t<T>, char8_t> || std::is_same_v<std::remove_cv_t<T>, char16_t> || std::is_same_v<std::remove_cv_t<T>, char32_t> || std::is_same_v<std::remove_cv_t<T>, wchar_t> || 
                                                std::is_same_v<std::remove_cv_t<T>, short> || std::is_same_v<std::remove_cv_t<T>, unsigned short> ||
                                                std::is_same_v<std::remove_cv_t<T>, int> || std::is_same_v<std::remove_cv_t<T>, unsigned int> ||
                                                std::is_same_v<std::remove_cv_t<T>, long> || std::is_same_v<std::remove_cv_t<T>, unsigned long> ||
                                                std::is_same_v<std::remove_cv_t<T>, long long> || std::is_same_v<std::remove_cv_t<T>, unsigned long long>> {};
    template<typename T> inline constexpr bool is_integral_v = is_integral<T>::value;

    template<typename T> struct is_floating_point : std::integral_constant<bool, std::is_same_v<std::remove_cv_t<T>, float> || std::is_same_v<std::remove_cv_t<T>, double> || std::is_same_v<std::remove_cv_t<T>, long double>>  {};
    template<typename T> inline constexpr bool is_floating_point_v = is_floating_point<T>::value;

    // Taken from https://en.cppreference.com/w/cpp/types/is_arithmetic
    template<typename T> struct is_arithmetic : std::integral_constant<bool, std::is_integral<T>::value || std::is_floating_point<T>::value> {};
    template<typename T> inline constexpr bool is_arithmetic_v = is_arithmetic<T>::value;

    // Taken from https://en.cppreference.com/w/cpp/types/is_lvalue_reference
    template<typename T> struct is_lvalue_reference : std::false_type {};
    template<typename T> struct is_lvalue_reference<T&> : std::true_type {};

    // Taken from https://en.cppreference.com/w/cpp/types/is_const
    template<typename T> struct is_const : std::false_type {};
    template<typename T> struct is_const<const T> : std::true_type {};

    // Taken from https://en.cppreference.com/w/cpp/types/is_void
    template<typename T> struct is_void : std::is_same<void, typename std::remove_cv<T>::type> {};

    // Taken from https://en.cppreference.com/w/cpp/types/is_reference
    template<typename T> struct is_reference : std::false_type {};
    template<typename T> struct is_reference<T&> : std::true_type {};
    template<typename T> struct is_reference<T&&> : std::true_type {};

    template<typename T> inline constexpr bool is_reference_v = is_reference<T>::value;

    // Taken from https://en.cppreference.com/w/cpp/types/is_array
    template<typename T> struct is_array : std::false_type {};
    template<typename T> struct is_array<T[]> : std::true_type {};
    template<typename T, size_t N> struct is_array<T[N]> : std::true_type {};

    template<typename T> inline constexpr bool is_array_v = is_array<T>::value;

    // Taken from https://en.cppreference.com/w/cpp/types/is_function
    template<typename T> struct is_function : std::integral_constant<bool, !std::is_const<const T>::value && !std::is_reference<T>::value> {};

    template<typename T> inline constexpr bool is_function_v = is_function<T>::value;

    // Taken from https://en.cppreference.com/w/cpp/types/type_identity
    template<typename T> struct type_identity { using type = T; };

    template<typename T>
    using type_identity_t = typename type_identity<T>::type;

    // Taken from https://en.cppreference.com/w/cpp/types/add_reference
    namespace detail
    {
        template<typename T>
        auto try_add_rvalue_reference(int) -> std::type_identity<T&&>;

        template<typename T>
        auto try_add_rvalue_reference(...) -> std::type_identity<T>;

        template <typename T>
        auto try_add_lvalue_reference(int) -> std::type_identity<T&>;

        template <typename T>
        auto try_add_lvalue_reference(...) -> std::type_identity<T>;
    } // namespace detail

    template<typename T> struct add_rvalue_reference : decltype(detail::try_add_rvalue_reference<T>(0)) {};

    template<typename T>
    using add_rvalue_reference_t = typename add_rvalue_reference<T>::type;

    template <typename T>
    struct add_lvalue_reference : decltype(detail::try_add_lvalue_reference<T>(0)) {};

    template<typename T>
    using add_lvalue_reference_t = typename add_lvalue_reference<T>::type;

    // Taken from https://en.cppreference.com/w/cpp/types/conditional
    template<bool B, typename T, typename F>
    struct conditional { typedef T type; };

    template<typename T, typename F>
    struct conditional<false, T, F> { typedef F type; };

    template<bool B, typename T, typename F>
    using conditional_t = typename conditional<B,T,F>::type;

    // Taken from https://en.cppreference.com/w/cpp/types/remove_extent
    template<typename T> struct remove_extent { typedef T type; };
    template<typename T> struct remove_extent<T[]> { typedef T type; };
    template<typename T, size_t N> struct remove_extent<T[N]> { typedef T type; };

    template<class T>
    using remove_extent_t = typename remove_extent<T>::type;

    // Taken from https://en.cppreference.com/w/cpp/types/add_pointer
    namespace detail {
        template <class T>
        auto try_add_pointer(int) -> std::type_identity<typename std::remove_reference<T>::type*>;
        template <class T>
        auto try_add_pointer(...) -> std::type_identity<T>;
    } // namespace detail
 
    template <typename T> struct add_pointer : decltype(detail::try_add_pointer<T>(0)) {};

    template<size_t L, size_t A>
    struct aligned_storage {
        struct type {
            alignas(A) unsigned char data[L];
        };
    };

    template<size_t L, size_t A = alignof(int)>
    using aligned_storage_t = typename aligned_storage<L, A>::type;

    template<typename T, typename... Args> struct is_constructible : std::integral_constant<bool, __is_constructible(T, Args...)> { };
    template<typename T, typename... Args> inline constexpr bool is_constructible_v = is_constructible<T, Args...>::value;

    template<typename T> struct is_trivially_copyable : std::integral_constant<bool, __has_trivial_copy(T)> { };
    template<typename T> inline constexpr bool is_trivially_copyable_v = is_trivially_copyable<T>::value;

    template<typename T> struct add_const { using type = const T; };
    template<typename T> using add_const_t = add_const<T>::type;

    // Taken from https://en.cppreference.com/w/cpp/types/is_copy_constructible
    template<typename T> struct is_copy_constructible : std::is_constructible<T, std::add_lvalue_reference_t<std::add_const_t<T>>> { };
    template<typename T> inline constexpr bool is_copy_constructible_v = is_copy_constructible<T>::value;

    // Taken from https://en.cppreference.com/w/cpp/types/is_default_constructible
    template<typename T> struct is_default_constructible : std::is_constructible<T> {};
    template<typename T> inline constexpr bool is_default_constructible_v = is_default_constructible<T>::value;
} // namespace std
