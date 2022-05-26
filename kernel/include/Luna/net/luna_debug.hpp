#pragma once

#include <Luna/common.hpp>
#include <Luna/misc/log.hpp>

#include <Luna/net/if.hpp>
#include <Luna/net/udp.hpp>

namespace net::luna_debug {
    struct Writer : public log::Logger {
        ~Writer() { flush(); }
        
        void putc(const char c) {
            buf[i++] = c;

            if(i == buf_size)
                flush();
        }

        void puts(const char* str, size_t len) {
            for(size_t i = 0; i < len; i++)
                putc(str[i]);
        }

		void flush() {
            Address a{};
            a.ip = {255, 255, 255, 255};
            a.mac = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
            a.port = 37020;

            std::span<uint8_t> packet{(uint8_t*)buf, i};

            udp::send(*net::get_default_if(), a, packet);

            i = 0;
        }

        private:
        static constexpr size_t buf_size = 100;
        char buf[buf_size];
        size_t i;
	};
} // namespace net::luna_debug
