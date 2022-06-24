#pragma once

#include <Luna/common.hpp>
#include <Luna/vmm/vm.hpp>

#include <Luna/misc/log.hpp>

namespace vm::e9 {
    struct Driver final : public vm::AbstractPIODriver {
        Driver(Vm* vm, log::Logger* logger): logger{logger} {
            vm->pio_map[0xe9] = this;
        }
        
        void pio_write(uint16_t port, uint32_t value, uint8_t size) {
            ASSERT(port == 0xe9);
            ASSERT(size == 1);

            logger->putc(value);
            if((char)value == '\n')
                logger->flush();
        }

        uint32_t pio_read(uint16_t port, uint8_t size) {
            ASSERT(port == 0xe9);
            ASSERT(size == 1);

            return 0xE9;
        }

        private:
        log::Logger* logger;
    };
} // namespace vm::e9
