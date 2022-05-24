#pragma once

namespace std {
    template<typename InputIt, typename T>
    constexpr InputIt find(InputIt first, InputIt last, const T& value) {
        for (; first != last; ++first)
            if (*first == value)
                return first;

        return last;
    }
} // namespace std
