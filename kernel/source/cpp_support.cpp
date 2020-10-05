#include <Luna/cpp_support.hpp>
#include <Luna/mm/hmm.hpp>

void* __dso_handle;
extern "C" int __cxa_atexit(void (*func) (void *), void * arg, void * dso_handle) {
    (void)func;
    (void)arg;
    (void)dso_handle;

    return 1;
}

void* operator new(size_t size){
    return (void*)hmm::alloc(size, 16);
}

void* operator new[](size_t size){
    return (void*)hmm::alloc(size, 16);
}

void operator delete(void* p){
    hmm::free((uintptr_t)p);
}

void operator delete[](void* p){
    hmm::free((uintptr_t)p);
}

void operator delete(void* p, long unsigned int size){
    (void)(size);
    hmm::free((uintptr_t)p);
}

void operator delete[](void* p, long unsigned int size){
    (void)(size);
    hmm::free((uintptr_t)p);
}