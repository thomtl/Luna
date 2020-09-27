#pragma once

#include <stdint.h>
#include <stddef.h>

#include <Luna/misc/misc.hpp>

#define PANIC(msg) panic(__FILE__, __PRETTY_FUNCTION__, __LINE__, msg)