#pragma once

#include <atomic>
#include <cstdint>

#include "driver/gpio.h"

#include "domain/types.hpp"

namespace ecotiter::infrastructure::drivers {

inline std::atomic<uint8_t> gValvePosition{0};

[[nodiscard]] domain::ValvePosition getGlobalValvePosition();
void setGlobalValvePosition(domain::ValvePosition pos);

class Valve {
public:
    explicit Valve(gpio_num_t pin);

    Valve(const Valve&) = delete;
    Valve& operator=(const Valve&) = delete;

    void setPosition(domain::ValvePosition position);
    [[nodiscard]] domain::ValvePosition getPosition() const noexcept;

private:
    gpio_num_t pin_;
    domain::ValvePosition position_{domain::ValvePosition::Input};
};

} // namespace ecotiter::infrastructure::drivers
