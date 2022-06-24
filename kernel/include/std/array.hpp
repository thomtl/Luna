#pragma once

#include <Luna/common.hpp>

namespace std {
    template<typename T, size_t N>
    struct array {
        using value_type = T;
        using size_type = size_t;
        using difference_type = ptrdiff_t;
        using reference = value_type&;
        using const_reference = const value_type&;
        using pointer = value_type*;
        using const_pointer = const value_type*;
        using iterator = pointer;
        using const_iterator = const_pointer;


        constexpr reference at(size_type pos) { ASSERT(pos < N); return _array[pos]; }
        constexpr const_reference at(size_type pos) const { ASSERT(pos < N); return _array[pos]; }

        constexpr reference operator[](size_type pos) { return _array[pos]; }
        constexpr const_reference operator[](size_type pos) const { return _array[pos]; }

        constexpr reference front() { return _array[0]; }
        constexpr const_reference front() const { return _array[0]; }

        constexpr reference back() { return _array[N - 1]; }
        constexpr const_reference back() const { return _array[N - 1]; }

        constexpr pointer data() { return _array; }
        constexpr const_pointer data() const { return _array; }

        constexpr iterator begin() { return _array; }
        constexpr const_iterator begin() const { return _array; }

        constexpr iterator end() { return _array + N; }
        constexpr const_iterator end() const { return _array + N; }

        [[nodiscard]]
        constexpr bool empty() const { return begin() == end(); }

        constexpr size_type size() const { return N; }

        private:
        T _array[N];
    };

    template<typename T>
    struct array<T, 0> {
        static_assert(std::dependent_false<T>::value);
    };

    template<size_t I, typename T, size_t N> T& get(std::array<T, N>& a) noexcept { static_assert(I < N); return a[I]; }
    template<size_t I, typename T, size_t N> const T& get(const std::array<T, N>& a) noexcept { static_assert(I < N); return a[I]; }
} // namespace std
