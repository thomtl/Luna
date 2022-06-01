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

        friend void swap(vector& a, vector& b) {
            using std::swap;
            swap(a._elements, b._elements);
            swap(a._size, b._size);
            swap(a._capacity, b._capacity);
        }

        constexpr vector(): _elements{nullptr}, _size{0}, _capacity{0} {}

        explicit vector(size_type count): vector() {
            resize(count);
        } 

        vector(const std::vector<T>& src): vector{} {
            ensure_capacity(src.size());
            for(size_t i = 0; i < src.size(); i++)
                new (&_elements[i]) T(src[i]);
            _size = src.size();
        }

        vector(vector&& other): vector{} {
            swap(*this, other);
        }

        vector& operator=(vector other) {
            swap(*this, other);
            return *this;
        }

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

        void reserve(size_type new_cap) {
            ensure_capacity(new_cap);
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

        std::span<T> span() { return std::span<T>{_elements, _size}; }

        struct reverse_iterator {
            reverse_iterator(iterator p): p{p} {}

            reference operator*() { return *p; }
            void operator++() { p--; }
            bool operator!=(reverse_iterator it) { return p != it.p; }

            private:
            iterator p;
        };

        reverse_iterator rbegin() { 
            if(_size == 0)
                return reverse_iterator{nullptr};
                
            return reverse_iterator{_elements + _size - 1}; 
        }

        reverse_iterator rend() { 
            if(_size == 0)
                return reverse_iterator{nullptr};
                
            return reverse_iterator{_elements - 1}; 
        }

        reference front() { return *begin(); }
        const_reference front() const { return *begin(); }

        reference back() { return *(end() - 1); }
        const_reference back() const { return *(end() - 1); }



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

        void pop_front() {
            erase(begin());
        }

        void pop_back() {
            _size--;
            _elements[_size].~T();
        }


        iterator erase(const_iterator pos) {
            auto iter = &_elements[pos - _elements];
            iter->~T();

            auto move_storage = [&](iterator dest, iterator from, size_t n) {
                if(dest < from) {
                    T* _dest = dest;
                    T* _from = from;
                    for(size_t i = 0; i < n; i++)
                        *_dest++ = std::move(*_from++);
                } else if(dest > from) {
                    T* _dest = dest + n - 1;
                    T* _from = from + n - 1;
                    for(size_t i = n; i > 0; i--)
                        *_dest-- = std::move(*_from--);
                }
            };

            move_storage(iter, iter + 1, _size - (iter - _elements));
            _size--;
            return iter;
        }

        iterator find(const T& v) {
            for(auto iter = begin(); iter != end(); ++iter)
                if(*iter == v)
                    return iter;

            return end();
        }

        const_iterator find(const T& v) const {
            for(auto iter = begin(); iter != end(); ++iter)
                if(*iter == v)
                    return iter;

            return end();
        }

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
