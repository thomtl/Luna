#pragma once

#include <stddef.h>

[[noreturn]]
void panic(const char* file, const char* func, size_t line, const char* msg);