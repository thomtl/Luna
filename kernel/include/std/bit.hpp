#pragma once

namespace std {
    template<typename T>
    constexpr int popcount(T x) noexcept {
        int pop = 0;
        for(unsigned int i = 0; i < (sizeof(T) * 8); i++)
            if(x & (1 << i))
                pop++;

        return pop;
    }
} // namespace std