#pragma once

namespace std {
    // Taken from https://en.cppreference.com/w/cpp/algorithm/find
    template<typename InputIt, typename T>
    constexpr InputIt find(InputIt first, InputIt last, const T& value) {
        for (; first != last; ++first)
            if (*first == value)
                return first;

        return last;
    }

    // Taken from https://en.cppreference.com/w/cpp/algorithm/find
    template<typename InputIt, typename UnaryPredicate>
    constexpr InputIt find_if(InputIt first, InputIt last, UnaryPredicate p) {
        for (; first != last; ++first)
            if (p(*first))
                return first;

        return last;
    }

    // Taken from https://en.cppreference.com/w/cpp/algorithm/fill_n
    template<typename OutputIt, typename Size, typename T>
    constexpr OutputIt fill_n(OutputIt first, Size count, const T& value) {
        for(Size i = 0; i < count; i++)
            *first++ = value;

        return first;
    }

    // Taken from https://en.cppreference.com/w/cpp/algorithm/generate
    template<typename ForwardIt, typename Generator>
    constexpr void generate(ForwardIt first, ForwardIt last, Generator g) {
        while(first != last)
            *first++ = g();
    }
} // namespace std
