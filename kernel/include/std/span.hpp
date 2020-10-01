#pragma once

#include <stddef.h>
#include <std/type_traits.hpp>
#include <std/memory.hpp>

namespace std
{
    constexpr size_t dynamic_extent = -1;
    template<typename T, size_t Extent = std::dynamic_extent>
    class span {
        public:
        static constexpr size_t extent = Extent;

        using element_type = T;
        using value_type = std::remove_cv_t<T>;
        using size_type = size_t;
        using different_type = ptrdiff_t;
        using pointer = T*;
        using const_pointer = const T*;
        using reference = T&;
        using const_reference = const T&;

        using iterator = T*;
        using const_iterator = const T*;

        constexpr span() : _data{nullptr}, _size{0} {}
        
        template<typename It>
        explicit(extent != std::dynamic_extent)
        constexpr span(It first, size_type count): _data{std::to_address(first)}, _size{count} {}

        iterator begin() {
            return _data;
        }

        iterator end() {
            return _data + _size;
        }

        const_iterator begin() const {
            return _data;
        }

        const_iterator end() const {
            return _data + _size;
        }

        reference operator[](size_type i) {
            return data()[i];
        }

        const_reference operator[](size_type i) const {
            return data()[i];
        }

        pointer data() {
            return _data;
        }

        const_pointer data() const {
            return _data;
        }

        size_t size() const {
            return _size;
        }

        size_t size_bytes() const {
            return _size * sizeof(element_type);
        }

        bool empty() const {
            return size() == 0;
        }

        private:
        element_type* _data;
        size_type _size;
    };
} // namespace std
