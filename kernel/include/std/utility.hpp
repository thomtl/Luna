#pragma once

#include <Luna/common.hpp>

#include <std/type_traits.hpp>
#include <std/bits/declval.hpp>
#include <std/bits/move.hpp>
#include <std/functional.hpp>

namespace std {
    template<typename T, T... Ints>
    struct integer_sequence {
        using value_type = T;
        static constexpr size_t size() noexcept { return sizeof...(Ints); }
    };

    template<size_t... Ints> using index_sequence = std::integer_sequence<size_t, Ints...>;
    template<typename T, T N> using make_integer_sequence = std::integer_sequence<T, __integer_pack(N)...>;
    template<size_t N> using make_index_sequence = std::make_integer_sequence<size_t, N>;
    template<typename... T> using index_sequence_for = std::make_index_sequence<sizeof...(T)>;

    template<typename T>
    constexpr void swap(T& a, T& b) {
        T tmp{move(a)};
        a = move(b);
        b = move(tmp);
    }

    template<typename T1, typename T2>
    struct pair {
        using first_type = T1;
        using second_type = T2;

        constexpr pair() = default;

        constexpr pair(const T1& x, const T2& y): first(x), second(y) {}

        template<typename U1, typename U2>
        constexpr pair(U1&& x, U2&& y): first(std::forward<U1>(x)), second(std::forward<U2>(y)) {}

        template<typename U1, typename U2>
        constexpr pair(const pair<U1, U2> p): first(p.first), second(p.second) {}

        template<typename U1, typename U2>
        constexpr pair(const pair<U1, U2>& p): first(p.first), second(p.second) {}

        template<typename U1, typename U2>
        constexpr pair(pair<U1, U2>&& p): first(std::forward<U1>(p.first)), second(std::forward<U2>(p.second)) {}

        pair(const pair& p) = default;
        pair(pair&& p) = default;

        constexpr pair& operator=(const pair& other){
            this->first = other.first;
            this->second = other.second;
            return *this;
        }

        template<typename U1, typename U2>
        constexpr pair& operator=(const pair<U1, U2>& other){
            this->first = other.first;
            this->second = other.second;
            return *this;
        }

        constexpr pair& operator=(pair&& other){
            this->first = std::move(other.first);
            this->second = std::move(other.second);
            return *this;
        }

        template<class U1, class U2>
        constexpr pair& operator=(pair<U1,U2>&& other){
            this->first = std::forward<U1>(other.first);
            this->second = std::forward<U2>(other.second);
            return *this;
        }

        //TODO: Comparison functions

        T1 first;
        T2 second;
    };

    template<typename T>
    struct tuple_size; // Forward declaration

    template<typename T1, typename T2>
    struct tuple_size<pair<T1, T2>> : std::integral_constant<size_t, 2> {};

    template<size_t I, typename T>
    struct tuple_element; // Forward decl

    template<size_t I, typename T>
    struct tuple_element<I, const T> {
        using type = std::add_const_t<typename std::tuple_element<I, T>::type>;
    };

    template<size_t I, typename T1, typename T2> struct tuple_element<I, std::pair<T1, T2>> { static_assert(std::dependent_false<T1>::value, "tuple_element for std::pair only has 2 elements"); };
    template<typename T1, typename T2> struct tuple_element<0, std::pair<T1, T2>> { using type = T1; };
    template<typename T1, typename T2> struct tuple_element<1, std::pair<T1, T2>> { using type = T2; };

    /*template<typename T1, typename T2> T1& get<0, T1, T2>(std::pair<T1, T2>& item) { return item.first; }
    template<typename T1, typename T2> const T1& get<0, T1, T2>(const std::pair<T1, T2>& item) { return item.first; }

    template<typename T1, typename T2> T2& get<0, T1, T2>(std::pair<T1, T2>& item) { return item.second; }
    template<typename T1, typename T2> const T2& get<0, T1, T2>(const std::pair<T1, T2>& item) { return item.second; }*/

    template<size_t I, typename T1, typename T2>
    constexpr std::tuple_element<I, std::pair<T1, T2>>::type& get(std::pair<T1, T2>& item) { if constexpr(I == 0) { return item.first; } else { return item.second; } };

    template<size_t I, typename T1, typename T2>
    constexpr const std::tuple_element<I, std::pair<T1, T2>>::type& get(const std::pair<T1, T2>& item) { if constexpr(I == 0) { return item.first; } else { return item.second; } };

    template<typename T>
    class lazy_initializer {
        public:
        constexpr lazy_initializer(): _initialized{false}, _storage{} {}

        template<typename... Args>
        T& init(Args&&... args){
            if(_initialized)
                return *get();

            new (&_storage) T{std::forward<Args>(args)...};
            _initialized = true;

            return *get();
        }

        explicit operator bool() {
            return _initialized;
        }

        T* operator ->(){
            return get();
        }

        T& operator *(){
            return *get();
        }

        T* get(){
            if(_initialized)
                return reinterpret_cast<T*>(&_storage);

            PANIC("Tried to get() uninitialized variable");
        }

        public:
        bool _initialized;
        std::aligned_storage_t<sizeof(T), alignof(T)> _storage;
    };

    template<>
    struct hash<std::pair<uint64_t, uint64_t>> {
        size_t operator()(const std::pair<uint64_t, uint64_t> v) const {
            size_t seed = v.first;
            seed ^= std::hash<uint64_t>{}(v.second) + 0x9E3779B9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };

    namespace impl {
        template<typename T>
        struct reverse_wrapper {
            T& it;

            auto begin() { return it.rbegin(); }
            auto end() { return it.rend(); }
        };
    } // namespace impl

    template<typename T>
    impl::reverse_wrapper<T> reverse(T& it) { return {it}; }

    template<size_t N, typename F>
    void comptime_iterate_to(F&& f) {
        [&]<size_t... Is>(std::index_sequence<Is...>) {
            (f.template operator()<Is>(), ...);
        }(std::make_index_sequence<N>{});
    }
} // namespace std
