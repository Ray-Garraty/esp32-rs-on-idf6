#include "infrastructure/drivers/valve.hpp"
#include "esp_log.h"

static constexpr auto TAG = "valve";

namespace ecotiter::infrastructure::drivers {

domain::ValvePosition getGlobalValvePosition() {
    uint8_t disc = gValvePosition.load(std::memory_order_acquire);
    return disc == 0 ? domain::ValvePosition::Input : domain::ValvePosition::Output;
}

void setGlobalValvePosition(domain::ValvePosition pos) {
    uint8_t disc = (pos == domain::ValvePosition::Output) ? 1 : 0;
    gValvePosition.store(disc, std::memory_order_release);
}

Valve::Valve(gpio_num_t pin)
    : pin_(pin) {
    gpio_set_direction(pin_, GPIO_MODE_OUTPUT);
    gpio_set_level(pin_, 0); // Input position (LOW)
}

void Valve::setPosition(domain::ValvePosition position) {
    switch (position) {
        case domain::ValvePosition::Input:
            gpio_set_level(pin_, 0);
            break;
        case domain::ValvePosition::Output:
            gpio_set_level(pin_, 1);
            break;
    }
    position_ = position;
}

domain::ValvePosition Valve::getPosition() const noexcept {
    return position_;
}

} // namespace ecotiter::infrastructure::drivers
