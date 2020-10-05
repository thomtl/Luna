#pragma once

#include <std/utility.hpp>
#include <std/vector.hpp>

namespace std
{
    template<typename T, size_t N>
    class minimal_vector {
        size_t _size;
        T _array[N];
        std::lazy_initializer<std::vector<T>> _vector;

        public:
        constexpr minimal_vector(): _size{0} {}

        size_t length() { return _size; }

        [[nodiscard]]
        T& operator[](size_t i){
            if(i < N)
                return _array[i];
            else {
                if(!_vector)
                    _vector.init();

                return _vector->operator[](i - N);
            }
        }

        T& push_back(const T& v){
            if(_size < N) {
                _array[_size++] = v;
                return _array[_size - 1];
            } else {
                if(!_vector)
                    _vector.init();

                _size++;
                return _vector->push_back(v);
            }
        }
    };
} // namespace std