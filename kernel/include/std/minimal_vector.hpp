#pragma once

#include <std/utility.hpp>
#include <std/vector.hpp>
#include <std/linked_list.hpp>

namespace std
{
    template<typename T, size_t N>
    class minimal_vector {
        public:
        constexpr minimal_vector(): _size{0} {}

        size_t length() const { return _size; }

        [[nodiscard]]
        T& operator[](size_t i){
            if(i < N)
                return *_array[i];
            else {
                if(!_vector)
                    _vector.init();

                return _vector->operator[](i - N);
            }
        }

        template<typename... Args>
        T& emplace_back(Args&&... args){
            if(_size < N) {
                _array[_size++].init(std::forward<Args>(args)...);
                return *_array[_size - 1];
            } else {
                if(!_vector)
                    _vector.init();

                _size++;
                return _vector->emplace_back(std::forward<Args>(args)...);
            }
        }

        private:
        size_t _size;
        std::lazy_initializer<T> _array[N];
        std::lazy_initializer<std::linked_list<T>> _vector;
    };
} // namespace std