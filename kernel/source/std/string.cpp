#include <std/string.hpp>

extern "C" size_t strlen(const char* str) {
    size_t s = 0;
    while(*str++)
        s++;
    return s;
}

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

extern "C" int memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* a = (const uint8_t*)s1;
    const uint8_t* b = (const uint8_t*)s2;

    for(size_t i = 0; i < n; i++) {
        if(a[i] < b[i])
            return -1;
        else if(b[i] < a[i])
            return 1;
    }

    return 0;
}

extern "C" void* memcpy(void* dst, const void* src, size_t n) {
    uint8_t* _dst = (uint8_t*)dst;
    const uint8_t* _src = (const uint8_t*)src;

    for(size_t i = 0; i < n; i++)
        _dst[i] = _src[i];
    
    return dst;
}

extern "C" bool isdigit(char c) {
    return (c >= '0') && (c <= '9');
}

extern "C" int64_t atoi(const char* c) {
    int64_t value = 0;
    int sign = 1;
    if(*c == '+' || *c == '-') {
        if(*c == '-') sign = -1;
        c++;
    }

    while(isdigit(*c)) {
        value *= 10;
        value += (int)(*c - '0');
        c++;
    }

    return value * sign;
}