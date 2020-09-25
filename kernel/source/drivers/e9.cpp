#include <Luna/drivers/e9.hpp>

bool E9::init(){
    return pio::inb(port_addr) == expected_value;
}

void E9::Writer::putc(const char c) const {
    pio::outb(port_addr, c);
}