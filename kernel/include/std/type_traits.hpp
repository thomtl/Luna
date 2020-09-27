#pragma once

namespace std
{
    template<typename T> struct remove_reference { typedef T type; };
    template<typename T> struct remove_reference<T&> { typedef T type; };
    template<typename T> struct remove_reference<T&&> { typedef T type; };

    template<typename T>
    using remove_reference_t = typename remove_reference<T>::type;

    template<typename T, T v>
    struct integral_constant {
        static constexpr T value = v;
        using value_type = T;
        using type = integral_constant; // using injected-class-name
        constexpr operator value_type() const noexcept { return value; }
        constexpr value_type operator()() const noexcept { return value; } //since c++14
    };

    using true_type = integral_constant<bool, true>;
    using false_type = integral_constant<bool, false>;

    template<typename T> struct is_lvalue_reference : std::false_type {};
    template<typename T> struct is_lvalue_reference<T&> : std::true_type {};

    template<size_t L, size_t A>
    struct aligned_storage {
        struct type {
            alignas(A) unsigned char data[L];
        };
    };

    template<size_t L, size_t A = alignof(int)>
    using aligned_storage_t = typename aligned_storage<L, A>::type;
} // namespace std
