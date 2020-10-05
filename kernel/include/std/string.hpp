#pragma once

#include <Luna/common.hpp>

extern "C" {
    int strncmp(const char* s1, const char* s2, size_t n);
    void* memset(void* s, int c, size_t n);
    void* memcpy(void* dst, const void* src, size_t n);
}