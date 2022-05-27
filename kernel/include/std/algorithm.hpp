#pragma once

namespace std {
    template<typename InputIt, typename T>
    constexpr InputIt find(InputIt first, InputIt last, const T& value) {
        for (; first != last; ++first)
            if (*first == value)
                return first;

        return last;
    }

    template<typename InputIt, typename UnaryPredicate>
    constexpr InputIt find_if(InputIt first, InputIt last, UnaryPredicate p) {
        for (; first != last; ++first)
            if (p(*first))
                return first;

        return last;
    }
} // namespace std
