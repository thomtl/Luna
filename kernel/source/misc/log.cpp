#include <Luna/misc/log.hpp>

#include <Luna/drivers/gpu/tty.hpp>

#include <Luna/drivers/e9.hpp>
#include <Luna/drivers/uart.hpp>
#include <Luna/drivers/vga.hpp>

log::Logger* log::global_logger;

std::lazy_initializer<uart::Writer> early_logger;
std::lazy_initializer<tty::Writer> late_logger;

void log::select_logger(log::LoggerType type) {
    switch (type) {
        case LoggerType::Early:
            if(!early_logger)
                early_logger.init(uart::com1_base);

            global_logger = early_logger.get();
            break;
        case LoggerType::Late:
            if(!late_logger)
                late_logger.init();

            global_logger = late_logger.get();
            break;
    }
}