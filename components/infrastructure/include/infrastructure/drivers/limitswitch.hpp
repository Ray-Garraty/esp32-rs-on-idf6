#pragma once

#include <atomic>
#include <cstdint>
#include <expected>

#include "driver/gpio.h"

#include "domain/errors.hpp"

namespace ecotiter::infrastructure::drivers {

inline std::atomic<bool> gStopFull{false};
inline std::atomic<bool> gStopHome{false};

class LimitSwitch {
public:
    LimitSwitch(gpio_num_t pin, std::atomic<bool>& flag, bool pullDown);
    ~LimitSwitch();

    LimitSwitch(const LimitSwitch&) = delete;
    LimitSwitch& operator=(const LimitSwitch&) = delete;

    [[nodiscard]] bool isTriggered() const;
    void clear();
    [[nodiscard]] domain::Result<void, domain::StepperError> rearm();
    [[nodiscard]] bool level() const;

private:
    gpio_num_t pin_;
    std::atomic<bool>& flag_;
};

} // namespace ecotiter::infrastructure::drivers
