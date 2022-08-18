#include <Luna/drivers/timers/hpet.hpp>
#include <Luna/drivers/pci.hpp>
#include <Luna/drivers/ioapic.hpp>

#include <Luna/cpu/cpu.hpp>
#include <Luna/cpu/idt.hpp>

#include <Luna/misc/log.hpp>
#include <std/linked_list.hpp>
#include <std/algorithm.hpp>

constexpr uint64_t femto_per_milli = 1'000'000'000'000ull;
constexpr uint64_t femto_per_micro = 1'000'000'000ull;
constexpr uint64_t femto_per_nano = 1'000'000ull;

static constinit IrqTicketLock lock;
static constinit std::linked_list<hpet::Device> devices;
static constinit std::vector<std::pair<hpet::Comparator*, bool>> periodic_comparators, oneshot_comparators; // Pointer, is_allocated

void hpet::init() {
    acpi::Hpet* table = nullptr;
    size_t i = 0;
    while((table = acpi::get_table<acpi::Hpet>(i))) {
        devices.emplace_back(table);

        i++;
    }
}

hpet::Device::Device(acpi::Hpet* table): table{table} {
    ASSERT(table->base.id == 0); // Assert it is in MMIO space
    regs = (volatile Regs*)(table->base.address + phys_mem_map);
    
    stop_timer();
    regs->cmd &= ~0b10; // Disable LegacyReplacementMode

    period = regs->cap >> 32;
    ASSERT(period <= 0x05F5E100);
    ASSERT(period > femto_per_nano);

    auto rev = regs->cap & 0xFF;
    ASSERT(rev != 0);

    auto vid = (regs->cap >> 16) & 0xFFFF;
    n_comparators = ((regs->cap >> 8) & 0x1F) + 1;

    regs->main_counter = 0; // Reset main counter
    regs->irq_status = regs->irq_status; // Clear all IRQs

    print("hpet: Timer Block {} v{}: VendorID: {:#x}, {} Timers\n", (uint16_t)table->uid, rev, vid, (uint16_t)n_comparators);

    uint32_t gsi_mask = ~0;
    for(size_t i = 0; i < n_comparators; i++) {
        auto& timer = comparators[i];
        timer.supports_fsb = (regs->comparators[i].cmd >> 15) & 1;
        timer.supports_periodic = (regs->comparators[i].cmd >> 4) & 1;
        timer.ioapic_route = (regs->comparators[i].cmd >> 32) & 0xFFFF'FFFF;

        print("      Comparator {}: FSB: {}, Periodic: {}, Route Cap: {:#b}\n", i, timer.supports_fsb, timer.supports_fsb, timer.ioapic_route);

        gsi_mask &= timer.ioapic_route;
    }

    print("      Common GSI mask: {:#b}\n", gsi_mask);
    uint32_t gsi = ~0;
    uint8_t gsi_vector;

    // Setup timers
    for(size_t i = 0; i < n_comparators; i++) {
        auto& timer = comparators[i];
        timer._device = this;
        timer._i = i;

        regs->comparators[i].cmd &= ~((1 << 14) | (0x1F << 9) | (1 << 8) | (1 << 6) | (1 << 3) | (1 << 2) | (1 << 1)); // Clear Periodic, IRQ enable, Level enable

        if(timer.supports_fsb) {
            auto vector = idt::allocate_vector();
            timer.vector = vector;

            idt::set_handler(vector, idt::Handler{.f = [](uint8_t, idt::Regs*, void* userptr){
                auto& comparator = *(hpet::Comparator*)userptr;
                auto& self = *comparator._device;

                comparator.lock.lock();

                if(!comparator.f) {
                    comparator.lock.unlock();
                    return;
                }

                auto f = comparator.f;
                auto usrptr = comparator.userptr;
                        
                if(!comparator.is_periodic) { // If One-shot make sure the IRQ doesn't happen again
                    self.regs->comparators[comparator._i].cmd &= ~(1 << 2);

                    comparator.userptr = nullptr;
                    comparator.f = nullptr;
                }
                comparator.lock.unlock();

                f(usrptr);
            }, .is_irq = true, .should_iret = true, .userptr = &timer});

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
                idt::set_handler(gsi_vector, idt::Handler{.f = [](uint8_t, idt::Regs*, void* userptr){
                    auto& self = *(hpet::Device*)userptr;
                    auto irq = self.regs->irq_status;

                    for(size_t i = 0; i < self.n_comparators; i++) {
                        if(irq & (1 << i)) {
                            auto& lock = self.comparators[i].lock;
                            lock.lock();
                            
                            if(!self.comparators[i].f) {
                                lock.unlock();
                                return;
                            }

                            auto f = self.comparators[i].f;
                            auto usrptr = self.comparators[i].userptr;
                        
                            if(!self.comparators[i].is_periodic) { // If One-shot make sure the IRQ doesn't happen again
                                self.regs->comparators[i].cmd &= ~(1 << 2);

                                self.comparators[i].userptr = nullptr;
                                self.comparators[i].f = nullptr;
                            }
                            self.regs->irq_status = (1 << i); // Clear IRQ status

                            lock.unlock();
                            f(usrptr);
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

        if(comparators[i].supports_periodic)
            periodic_comparators.emplace_back(&comparators[i], false); // false => Not allocated yet
        else
            oneshot_comparators.emplace_back(&comparators[i], false);
    }

    start_timer();
}

void hpet::Device::poll_sleep(const TimePoint& duration) {
    auto goal = regs->main_counter + ((duration.ns() * femto_per_nano) / period);

    while(regs->main_counter < goal)
        asm("pause");
}

uint64_t hpet::Device::time_ns() {
    return (regs->main_counter * period) / femto_per_nano;
}

bool hpet::Comparator::start_timer(bool periodic, const TimePoint& period, void(*f)(void*), void* userptr) {
    std::lock_guard guard{lock};

    if(!supports_periodic && periodic)
        return false;

    if(this->f)
        return false;

    auto delta = (period.ns() * femto_per_nano) / _device->period; // Order of multiplication alone does matter here due to integer division
    auto& reg = _device->regs->comparators[this->_i];
    reg.cmd &= ~((1 << 6) | (1 << 3) | (1 << 2));
    _device->regs->irq_status = (1 << this->_i);

    this->f = f;
    this->userptr = userptr;
    this->is_periodic = periodic;

    _device->stop_timer();

    if(periodic) {
        reg.cmd |= (1 << 6) | (1 << 3) | (1 << 2);
        reg.value = _device->regs->main_counter + delta;
        reg.value = delta;
    } else {
        reg.cmd |= (1 << 2); // Enable IRQ in One-shot mode
        reg.value = _device->regs->main_counter + delta;
    }

    _device->start_timer();

    return true;
}

void hpet::Comparator::cancel_timer() {
    std::lock_guard guard{lock};

    _device->regs->comparators[this->_i].cmd &= ~(1 << 2); // Stop generating IRQs
    this->f = nullptr;
}

void hpet::poll_sleep(const TimePoint& duration) { devices.front().poll_sleep(duration); }
uint64_t hpet::time_ns() { return devices.front().time_ns(); }

hpet::Comparator* hpet::allocate_comparator(bool require_periodic) {
    std::lock_guard guard{lock};

    if(!require_periodic) {
        auto it = std::find_if(oneshot_comparators.begin(), oneshot_comparators.end(), [](const auto& item) { return !item.second; });
        if(it != oneshot_comparators.end()) {
            it->second = true;
            return it->first;
        }
    }

    auto it = std::find_if(periodic_comparators.begin(), periodic_comparators.end(), [](const auto& item) { return !item.second; });
    if(it != periodic_comparators.end()) {
        it->second = true;
        return it->first;
    }

    return nullptr;
}