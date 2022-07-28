#pragma once

#include <Luna/common.hpp>
#include <std/string.hpp>

#include <std/bits/move.hpp>

namespace std {
    class bitmap {
        public:
        constexpr bitmap() = default;
        bitmap(size_t n) {
            n_bytes = div_ceil(n, 8);
            data = new uint8_t[n_bytes];

            memset(data, 0, n_bytes);
        }

        bitmap(const bitmap& other) {
            this->n_bytes = other.n_bytes;
            this->data = new uint8_t[other.n_bytes];
            memcpy(this->data, other.data, other.n_bytes);
        }

        bitmap& operator=(const bitmap& other) {
            this->n_bytes = other.n_bytes;
            this->data = new uint8_t[other.n_bytes];
            memcpy(this->data, other.data, other.n_bytes);

            return *this;
        }

        bitmap(bitmap&&) = delete;
        bitmap& operator=(bitmap&& other) {
            n_bytes = std::move(other.n_bytes);
            data = std::move(other.data);

            other.n_bytes = 0;
            other.data = nullptr;

            return *this;
        }

        ~bitmap() {
            delete[] data;
        }

        void set(size_t bit) {
            data[bit / 8] |= (1ull << (bit % 8));
        }

        void clear(size_t bit) {
            data[bit / 8] &= ~(1ull << (bit % 8));
        }

        bool test(size_t bit) {
            return (data[bit / 8] & (1ull << (bit % 8))) ? true : false;
        }

        size_t get_free_bit() {
            for(size_t i = 0; i < n_bytes; i++)
                if(data[i] != 0xFF)
                    for(uint8_t j = 0; j < 8; j++)
                        if(!(data[i] & (1ull << j)))
                            return i * 8 + j;

            return ~0;
        }

        private:
        size_t n_bytes;
        uint8_t* data;
    };
} // namespace std
