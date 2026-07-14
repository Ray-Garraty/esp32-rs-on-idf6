#include "infrastructure/drivers/valve.hpp"

namespace ecotiter::infrastructure::drivers {

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
