#include <Luna/cpu/pio.hpp>

uint8_t pio::inb(uint16_t port) {
    uint8_t ret;
    asm volatile("in %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void pio::outb(uint16_t port, uint8_t v) {
    asm volatile("out %0, %1" : : "a"(v), "Nd"(port));
}