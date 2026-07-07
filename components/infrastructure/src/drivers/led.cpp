#include "infrastructure/drivers/led.hpp"
#include "esp_log.h"

static constexpr auto TAG = "led";

namespace ecotiter::infrastructure::drivers {

Led::Led(gpio_num_t pin)
    : pin_(pin) {
    gpio_set_direction(pin_, GPIO_MODE_OUTPUT);
    gpio_set_level(pin_, 0);
}

void Led::setTransportMode(domain::TransportMode mode) {
    mode_ = mode;
    timerMs_ = 0;
    blinkState_ = false;

    switch (mode_) {
        case domain::TransportMode::UsbActive:
            gpio_set_level(pin_, 0);
            break;
        case domain::TransportMode::BleAdvertising:
            gpio_set_level(pin_, 1);
            break;
        case domain::TransportMode::BleConnected:
            gpio_set_level(pin_, 0);
            break;
    }
}

void Led::process(uint32_t elapsedMs) {
    switch (mode_) {
        case domain::TransportMode::UsbActive:
            gpio_set_level(pin_, 0);
            break;
        case domain::TransportMode::BleAdvertising:
            gpio_set_level(pin_, 1);
            break;
        case domain::TransportMode::BleConnected: {
            timerMs_ += elapsedMs;
            if (timerMs_ >= 500) {
                timerMs_ -= 500;
                blinkState_ = !blinkState_;
                gpio_set_level(pin_, blinkState_ ? 1 : 0);
            }
            break;
        }
    }
}

domain::TransportMode Led::currentMode() const noexcept {
    return mode_;
}

} // namespace ecotiter::infrastructure::drivers
