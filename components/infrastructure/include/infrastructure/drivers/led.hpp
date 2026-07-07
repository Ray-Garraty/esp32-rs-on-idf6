#pragma once

#include <cstdint>

#include "driver/gpio.h"

#include "domain/types.hpp"

namespace ecotiter::infrastructure::drivers {

class Led {
public:
    explicit Led(gpio_num_t pin);

    Led(const Led&) = delete;
    Led& operator=(const Led&) = delete;

    void setTransportMode(domain::TransportMode mode);
    void process(uint32_t elapsedMs);

    [[nodiscard]] domain::TransportMode currentMode() const noexcept;

private:
    gpio_num_t pin_;
    domain::TransportMode mode_{domain::TransportMode::UsbActive};
    uint32_t timerMs_{0};
    bool blinkState_{false};
};

} // namespace ecotiter::infrastructure::drivers
