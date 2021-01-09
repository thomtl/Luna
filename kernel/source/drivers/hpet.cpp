#include <Luna/drivers/hpet.hpp>
#include <Luna/drivers/pci.hpp>
#include <Luna/drivers/ioapic.hpp>

#include <Luna/cpu/idt.hpp>

#include <Luna/misc/log.hpp>
#include <std/linked_list.hpp>

constexpr uint64_t femto_per_milli = 1'000'000'000'000;

hpet::Device::Device(acpi::Hpet* table): table{table} {
    ASSERT(table->base.id == 0); // Assert it is in MMIO space
    regs = (volatile Regs*)(table->base.address + phys_mem_map);
    
    stop_timer();
    regs->cmd &= ~0b10; // Disable LegacyReplacementMode

    period = regs->cap >> 32;
    ASSERT(period <= 0x05F5E100);

    auto rev = regs->cap & 0xFF;
    ASSERT(rev != 0);

    auto vid = (regs->cap >> 16) & 0xFFFF;
    n_comparators = ((regs->cap >> 8) & 0x1F) + 1;

    regs->main_counter = 0; // Reset main counter
    regs->irq_status = regs->irq_status; // Clear all IRQs

    print("hpet: Timer Block {} v{}: VendorID: {:#x}, {} Timers\n", (uint16_t)table->uid, rev, vid, (uint16_t)n_comparators);

    uint32_t gsi_mask = ~0;
    for(size_t i = 0; i < n_comparators; i++) {
        auto& timer = timers[i];
        timer.fsb = (regs->comparators[i].cmd >> 15) & 1;
        timer.is_periodic = (regs->comparators[i].cmd >> 4) & 1;
        timer.ioapic_route = (regs->comparators[i].cmd >> 32) & 0xFFFF'FFFF;

        print("      Comparator {}: FSB: {}, Periodic: {}, Route Cap: {:#b}\n", i, timer.fsb, timer.is_periodic, timer.ioapic_route);

        gsi_mask &= timer.ioapic_route;
    }

    print("      Common GSI mask: {:#b}\n", gsi_mask);
    uint32_t gsi = ~0;
    uint8_t gsi_vector;

    // Setup IRQs
    for(size_t i = 0; i < n_comparators; i++) {
        auto& timer = timers[i];

        regs->comparators[i].cmd &= ~((1 << 14) | (0x1F << 9) | (1 << 8) | (1 << 6) | (1 << 3) | (1 << 2) | (1 << 1)); // Clear Periodic, IRQ enable, Level enable

        if(timer.fsb) {
            auto vector = idt::allocate_vector();
            timer.vector = vector;

            idt::set_handler(vector, idt::handler{.f = [](uint8_t vector, idt::regs*, void* userptr){
                auto& self = *(hpet::Device*)userptr;

                for(size_t i = 0; i < self.n_comparators; i++) {
                    if(self.timers[i].vector == vector) {
                        if(!self.timers[i].f)
                            return;

                        self.timers[i].f(self.timers[i].userptr);
                        
                        if(!self.timers[i].periodic) { // If One-shot make sure the IRQ doesn't happen again
                            self.regs->comparators[i].cmd &= ~(1 << 2);

                            self.timers[i].userptr = nullptr;
                            self.timers[i].f = nullptr;
                        }
                        return;
                    }
                }
            }, .is_irq = true, .should_iret = true, .userptr = this});

            pci::msi::Address addr{};
            pci::msi::Data data{};

            addr.base_address = 0xFEE;
            addr.destination_id = get_cpu().lapic_id & 0xFF;

            data.delivery_mode = 0;
            data.vector = vector;

            regs->comparators[i].fsb = ((uint64_t)addr.raw << 32) | data.raw;
            regs->comparators[i].cmd |= (1 << 14); // Enable FSB Mode
        } else {
            if(gsi == ~0u) {
                for(int i = 31; i >= 0; i--) {
                    if(gsi_mask & (1 << i)) {
                        gsi = i; 
                        break;
                    }
                }
                ASSERT(gsi != ~0u);

                gsi_vector = gsi + 0x20;
                idt::set_handler(gsi_vector, idt::handler{.f = [](uint8_t, idt::regs*, void* userptr){
                    auto& self = *(hpet::Device*)userptr;
                    auto irq = self.regs->irq_status;

                    for(size_t i = 0; i < self.n_comparators; i++) {
                        if(irq & (1 << i)) {
                            if(!self.timers[i].f)
                                return;

                            self.timers[i].f(self.timers[i].userptr);
                        
                            if(!self.timers[i].periodic) { // If One-shot make sure the IRQ doesn't happen again
                                self.regs->comparators[i].cmd &= ~(1 << 2);

                                self.timers[i].userptr = nullptr;
                                self.timers[i].f = nullptr;
                            }
                            self.regs->irq_status = (1 << i); // Clear IRQ status
                        }
                    }
                }, .is_irq = true, .should_iret = true, .userptr = this});

                ioapic::set(gsi, gsi_vector, ioapic::regs::DeliveryMode::Fixed, ioapic::regs::DestinationMode::Physical, 0x8, get_cpu().lapic_id);
                ioapic::unmask(gsi);
            }

            timer.vector = gsi_vector;

            regs->comparators[i].cmd |= ((gsi & 0x1F) << 9) | (1 << 1); // Set GSI and Level triggered

            ASSERT(((regs->comparators[i].cmd >> 9) & 0x1F) == gsi); // If GSI was successfully set the read value should equal the written value
        }
    }

    start_timer();
}

bool hpet::Device::start_timer(bool periodic, uint64_t ms, void(*f)(void*), void* userptr) {
    stop_timer();

    uint8_t timer_i = 0xFF;
    for(size_t i = 0; i < n_comparators; i++) {
        if(!timers[i].f) {
            if(periodic && !timers[i].is_periodic)
                continue;
            timer_i = i;
            break;
        }
    }

    if(timer_i == 0xFF) {
        start_timer();
        return false;
    }

    auto& timer = timers[timer_i];

    timer.f = f;
    timer.userptr = userptr;
    timer.periodic = periodic;

    auto& reg = regs->comparators[timer_i];
    reg.cmd &= ~((1 << 6) | (1 << 3) | (1 << 2));

    auto delta = ms * (femto_per_milli / period);

    if(periodic) {
        reg.cmd |= (1 << 6) | (1 << 3) | (1 << 2);
        reg.value = regs->main_counter + delta;
        reg.value = delta;
    } else {
        reg.cmd |= (1 << 2); // Enable IRQ in One-shot mode
        reg.value = regs->main_counter + delta;
    }

    start_timer();

    return true;
}

void hpet::Device::poll_sleep(uint64_t ms) {
    auto goal = regs->main_counter + (ms * (femto_per_milli / period));

    while(regs->main_counter < goal)
        asm("pause");
}


static std::linked_list<hpet::Device> devices;

void hpet::init() {
    acpi::Hpet* table = nullptr;
    size_t i = 0;
    while((table = acpi::get_table<acpi::Hpet>(i))) {
        devices.emplace_back(table);

        i++;
    }
}

void hpet::poll_sleep(uint64_t ms) {
    // For polling it doesn't really matter which one we pick, so just take the first one
    ASSERT(devices.size() >= 1);

    devices[0].poll_sleep(ms);
}

bool hpet::start_timer(bool periodic, uint64_t ms, void(*f)(void*), void* userptr) {
    ASSERT(devices.size() >= 1);

    for(auto& device : devices) {
        if(device.start_timer(periodic, ms, f, userptr))
            return true;
    }

    return false; // Was not able to find a free slot for a timer
}