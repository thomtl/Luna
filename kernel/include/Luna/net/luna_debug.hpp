#pragma once

#include <Luna/common.hpp>
#include <Luna/misc/log.hpp>

#include <Luna/net/if.hpp>
#include <Luna/net/udp.hpp>

namespace net::luna_debug {
    struct Writer : public log::Logger {
        void putc(const char c) const {
            buf[i++] = c;
        }

		void flush() const {
            Address a{};
            a.ip = {255, 255, 255, 255};
            a.mac = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
            a.port = 37020;

            std::span<uint8_t> packet{(uint8_t*)buf, i};

            udp::send(*net::get_default_if(), a, packet);
        }

        private:
        static constexpr size_t buf_size = 100;
        mutable char buf[buf_size];
        mutable size_t i;
	};
} // namespace net::luna_debug
