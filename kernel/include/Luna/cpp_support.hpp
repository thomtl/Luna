#pragma once

#include <stddef.h>

inline void* operator new(size_t, void* p) { return p; }
inline void* operator new[](size_t, void* p) { return p; }
inline void  operator delete(void*, void*) {}
inline void  operator delete[](void*, void*) {}

void* operator new(size_t size);
void* operator new[](size_t size);
void operator delete(void* p);
void operator delete[](void* p);
void operator delete(void* p, long unsigned int size);
void operator delete[](void* p, long unsigned int size);