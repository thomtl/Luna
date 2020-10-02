#include <std/string.hpp>

extern "C" int strncmp(const char* s1, const char* s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)){
        ++s1;
        ++s2;
        --n;
    }

    if (n == 0) {
        return 0;
    } else {
        return (*(unsigned char *)s1 - *(unsigned char *)s2);
    }
}

extern "C" void* memset(void* s, int c, size_t n) {
    uint8_t* buf = (uint8_t*)s;

    for(size_t i = 0; i < n; i++)
        buf[i] = (uint8_t)c;

    return s;
}