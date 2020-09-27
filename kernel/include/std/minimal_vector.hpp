#pragma once

#include <std/utility.hpp>

namespace std
{
    template<typename T, size_t N>
    class minimal_vector {
        size_t curr_i;
        T array[N];
        // TODO: Add dynamic part

        public:
        constexpr minimal_vector(): curr_i{0} {}

        size_t length() { return curr_i; }

        [[nodiscard]]
        T& operator[](size_t i){
            if(i < N)
                return array[i];
            else
                PANIC("//TODO: Implement dynamic part");
        }

        T& push_back(const T& v){
            if(curr_i < N) {
                array[curr_i++] = v;
                return array[curr_i - 1];
            } else
                PANIC("//TODO: Implement dynamic part");
        }
    };
} // namespace std