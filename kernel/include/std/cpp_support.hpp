#pragma once

#include <stddef.h>

inline void* operator new(size_t, void* p) { return p; }
inline void* operator new[](size_t, void* p) { return p; }
inline void  operator delete(void*, void*) {};
inline void  operator delete[](void*, void*) {};