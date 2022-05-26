#pragma once

#include <Luna/common.hpp>
#include <Luna/drivers/gpu/gpu.hpp>

#include <Luna/misc/log.hpp>

namespace tty {
    struct Writer : public log::Logger {
        Writer();
        void putc(const char c);
        void flush();

        private:
        size_t x, y;
    };
} // namespace tty
