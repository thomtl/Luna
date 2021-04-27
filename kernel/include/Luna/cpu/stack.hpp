#pragma once

#include <Luna/common.hpp>
#include <Luna/mm/hmm.hpp>
#include <std/string.hpp>

namespace cpu {
    struct Stack {
        Stack(size_t size): _size{size} {
            _bottom = (uint8_t*)hmm::alloc(_size, 16);    
            memset(_bottom, 0, _size);

            _top = _bottom + _size;
        }

        ~Stack() {
            hmm::free((uintptr_t)_bottom);
        }

        Stack(const Stack&) = delete;
        Stack(Stack&& b) = delete;
        Stack& operator=(Stack other) = delete;

        void* top() { return _top; }
        void* bottom() { return _bottom; }

        size_t size() const { return _size; }

        template<typename T, typename... Args>
        T* push(Args&&... args) {
            _top -= align_up(sizeof(T), max(16, alignof(T)));
            return new (_top) T(std::forward<Args>(args)...);
        }

        private:
        size_t _size;
        uint8_t* _bottom, *_top;
    };
} // namespace cpu
