#pragma once

#include <Luna/common.hpp>

#include <std/type_traits.hpp>
#include <std/bits/declval.hpp>
#include <std/functional.hpp>

namespace std
{
    template<typename T>
    constexpr typename std::remove_reference<T>::type&& move(T&& t) noexcept {
        return static_cast<typename std::remove_reference_t<T>&&>(t);
    }

    template<typename T>
    constexpr T&& forward(typename std::remove_reference<T>::type& t) noexcept {
        return static_cast<T&&>(t);
    }

    template<typename T>
    constexpr T&& forward(typename std::remove_reference<T>::type&& t) noexcept {
        static_assert(!std::is_lvalue_reference<T>::value);
        return static_cast<T&&>(t);
    }

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

    template<typename T>
    class lazy_initializer {
        public:
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
} // namespace std
