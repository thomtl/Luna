#pragma once

#include <Luna/common.hpp>
#include <std/bits/move.hpp>
#include <std/type_traits.hpp>

namespace std {
    namespace detail {
        template<typename... Types>
        struct tuple_storage;

        template<typename T, typename... Types>
        struct tuple_storage<T, Types...> {
            constexpr tuple_storage() requires(std::is_default_constructible_v<Types> && ...) = default;

            template<typename U, typename... UTypes>
            constexpr tuple_storage(U&& item, UTypes&&... args): item{std::forward<U>(item)}, storage{std::forward<UTypes>(args)...} {}

            T item;
            tuple_storage<Types...> storage;
        };

        template<>
        struct tuple_storage<> { };

        /*template<typename T>
        struct tuple_storage<T> {
            template<typename U>
            constexpr tuple_storage(U&& item): item{std::forward<U>(item)} {}

            T item;
        };*/

        template<size_t I, typename T, typename... Types>
        struct nth_type { using type = nth_type<I - 1, Types...>::type; };

        template<typename T, typename... Types>
        struct nth_type<0, T, Types...> { using type = T; };

        template<size_t I, typename... Types>
        using nth_type_t = nth_type<I, Types...>::type;

        template<size_t I, typename T, typename... Types>
        struct accessor {
            static constexpr nth_type_t<I - 1, Types...>& access(tuple_storage<T, Types...>& storage) { return accessor<I - 1, Types...>::access(storage.storage); }
            static constexpr const nth_type_t<I - 1, Types...>& access(const tuple_storage<T, Types...>& storage) { return accessor<I - 1, Types...>::access(storage.storage); }
        };

        template<typename T, typename... Types>
        struct accessor<0, T, Types...> {
            static constexpr T& access(tuple_storage<T, Types...>& storage) { return storage.item; }
            static constexpr const T& access(const tuple_storage<T, Types...>& storage) { return storage.item; }
        };
    } // namespace detail

    template<typename... Types>
    class tuple {
        public:
        constexpr tuple() = default;

        template<typename... UTypes> requires (sizeof...(Types) == sizeof...(UTypes) && sizeof...(Types) >= 1 && (std::is_constructible_v<Types, UTypes> && ...))
        explicit((!std::is_convertible_v<Types, UTypes> || ...))
        constexpr tuple(UTypes&&... args): items{std::forward<UTypes>(args)...} { }

        //private:
        detail::tuple_storage<Types...> items;
    };

    // Taken from https://en.cppreference.com/w/cpp/utility/tuple/tuple_element
    template<size_t I, typename T>
    struct tuple_element;

    template<size_t I, typename Head, typename... Tail>
    struct tuple_element<I, std::tuple<Head, Tail...>> : std::tuple_element<I - 1, std::tuple<Tail...>> { };

    template<typename Head, typename... Tail>
    struct tuple_element<0, std::tuple<Head, Tail...>> { using type = Head; };
    
    template<typename T>
    struct tuple_size; // Forward decl

    template<typename... Types>
    struct tuple_size<std::tuple<Types...>> : std::integral_constant<size_t, sizeof...(Types)> {};

    template<typename T> struct tuple_size<const T> : std::integral_constant<size_t, tuple_size<T>::value> {};
    template<typename T> struct tuple_size<volatile T> : std::integral_constant<size_t, tuple_size<T>::value> {};
    template<typename T> struct tuple_size<const volatile T> : std::integral_constant<size_t, tuple_size<T>::value> {};

    template<typename T> inline constexpr size_t tuple_size_v = tuple_size<T>::value;

    template<size_t I, typename... Types>
    typename std::tuple_element<I, tuple<Types...>>::type& get(tuple<Types...>& t) noexcept { return detail::accessor<I, Types...>::access(t.items); }

    template<size_t I, typename... Types>
    typename std::tuple_element<I, tuple<Types...>>::type const& get(const tuple<Types...>& t) noexcept { return detail::accessor<I, Types...>::access(t.items); }

    // Taken from https://en.cppreference.com/w/cpp/utility/apply
    namespace detail {
        template<typename F, typename Tuple, size_t... I>
        constexpr decltype(auto) apply_impl(F&& f, Tuple&& t, std::index_sequence<I...>) {
            // TODO: Use std::invoke
            return std::forward<F>(f)(std::get<I>(std::forward<Tuple>(t))...);
        }
    } // namespace detail
    

    template<typename F, typename Tuple>
    constexpr decltype(auto) apply(F&& f, Tuple&& t) {
        return detail::apply_impl(std::forward<F>(f), std::forward<Tuple>(t), std::make_index_sequence<std::tuple_size_v<std::remove_reference_t<Tuple>>>{});
    }
} // namespace std
