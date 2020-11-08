#pragma once

#include <stddef.h>
#include <std/utility.hpp>
#include <Luna/mm/hmm.hpp>

namespace std
{
    template<typename T>
    class vector {
        public:
        using value_type = T;
        using size_type = size_t;
        using difference_type = ptrdiff_t;
        using reference = value_type&;
        using const_reference = const value_type&;
        using pointer = value_type*;
        using const_pointer = const value_type*;

        using iterator = pointer;
        using const_iterator = const_pointer;

        constexpr vector(): _elements{nullptr}, _size{0}, _capacity{0} {}
        vector(const std::vector<T>& src) {
            ensure_capacity(src.size());
            for(const auto& e : src)
                push_back(e);
        }

        vector(vector&&) = delete;
        vector& operator=(vector other) = delete;

        ~vector() {
            for(size_t i = 0; i < _size; i++)
                _elements[i].~T();
            hmm::free((uintptr_t)_elements);
        }

        template<typename... Args>
        void resize(size_t size, Args&&... args) {
            ensure_capacity(size);
	        if(size < _size) {
		        for(size_t i = size; i < _size; i++)
			        _elements[i].~T();
	        } else {
		        for(size_t i = _size; i < size; i++)
			        new (&_elements[i]) T(std::forward<Args>(args)...);
	        }
	        _size = size;
        }

        void clear() {
            for(size_t i = 0; i < _size; i++)
                _elements[i].~T();
            _size = 0;
        }

        bool empty() const { return _size == 0; }
        size_type size() const { return _size; }
        size_type capacity() const { return _capacity; }
        pointer data() { return _elements; }
        const_pointer data() const { return _elements; }

        iterator begin() { return _elements; }
        const_iterator begin() const { return _elements; }
        iterator end() { return _elements + _size; }
        const_iterator end() const { return _elements + _size; }

        reference push_back(const_reference value) {
            ensure_capacity(_size + 1);
            T* ret = new (&_elements[_size]) T(value);
            _size++;
            return *ret;
        }

        template<typename... Args>
        reference emplace_back(Args&&... args) {
            ensure_capacity(_size + 1);
            T* ret = new (&_elements[_size]) T(std::forward<Args>(args)...);
            _size++;
            return *ret;
        }

        const_reference operator[](size_t index) const { return _elements[index]; }
        reference operator[](size_t index) { return _elements[index]; }

        private:
        void ensure_capacity(size_t c) {
            if(c <= _capacity)
                return;

            size_t new_capacity = c * 2;
            auto* new_array = (pointer)hmm::alloc(new_capacity * sizeof(value_type), alignof(value_type));
            for(size_t i = 0; i < _capacity; i++)
                new (&new_array[i]) T(std::move(_elements[i]));

            for(size_t i = 0; i < _size; i++)
                _elements[i].~T();

            hmm::free((uintptr_t)_elements);

            _elements = new_array;
            _capacity = new_capacity;
        }

        T* _elements;
        size_t _size, _capacity;
    };
} // namespace std
