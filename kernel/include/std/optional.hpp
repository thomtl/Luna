#pragma once

#include <std/bits/traits.hpp>
#include <std/bits/move.hpp>

namespace std {
    // Taken from https://en.cppreference.com/w/cpp/utility/optional/nullopt_t
    struct nullopt_t {
        explicit constexpr nullopt_t(int) {}
    };
    inline constexpr nullopt_t nullopt{0};
    

    template<typename T>
    class optional {
        public:
        using value_type = T;

        constexpr optional() noexcept : _storage{}, _constructed{false} {}
        constexpr optional(std::nullopt_t) noexcept : _storage{}, _constructed{false} {}

        template<typename U = T> requires(std::is_constructible_v<T, U&&>)
        explicit(!std::is_convertible_v<U&&, T>)
        constexpr optional(U&& value) {
            new (_storage.data) T(value);
            _constructed = true;
        }

        constexpr optional(const optional&) requires(std::is_trivially_copy_constructible_v<T>) = default;
        constexpr optional(const optional&) = delete;

        constexpr ~optional() requires(!std::is_trivially_destructible_v<T>) {
            if(_constructed)
                value().T::~T();
        }

        constexpr ~optional() = default;

        constexpr T* operator->() noexcept { return (T*)(_storage.data); }
        constexpr const T* operator->() const noexcept { return (const T*)(_storage.data); }

        constexpr T& operator*() & noexcept { return *(T*)(_storage.data); }
        constexpr const T& operator*() const & noexcept { return *(const T*)(_storage.data); }

        //constexpr T&& operator*() && noexcept { return *(T*)(_storage.data); }
        //constexpr const T&& operator*() const && noexcept { return *(const T*)(_storage.data); }

        constexpr T& value() & { ASSERT(_constructed); return *(T*)(_storage.data); }
        constexpr const T& value() const & { ASSERT(_constructed); return *(const T*)(_storage.data); }

        //constexpr T&& value() && { ASSERT(_constructed); return *(T*)(_storage.data); }
        //constexpr const T&& value() const && { ASSERT(_constructed); return *(const T*)(_storage.data); }

        constexpr explicit operator bool() const noexcept { return _constructed; }
        constexpr bool has_value() const noexcept { return _constructed; }

        template<typename U>
        constexpr T value_or(U&& default_value) const & { return has_value() ? **this : static_cast<T>(std::forward<U>(default_value)); }

        template<typename U>
        constexpr T value_or(U&& default_value) && { return has_value() ? std::move(**this) : static_cast<T>(std::forward<U>(default_value)); }


        private:
        std::aligned_storage_t<sizeof(T), alignof(T)> _storage;
        bool _constructed;
    };
} // namespace std
