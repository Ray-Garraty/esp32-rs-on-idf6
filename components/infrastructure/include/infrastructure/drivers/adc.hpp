#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <expected>

#include "esp_adc/adc_oneshot.h"

#include "domain/errors.hpp"

namespace ecotiter::infrastructure::drivers {

class AdcDriver; // forward decl for gAdcDriver

inline std::atomic<uint16_t> gCoeffAX1000{1000};
inline std::atomic<int16_t> gCoeffB{0};
inline AdcDriver* gAdcDriver{nullptr};

[[nodiscard]] int16_t calibratedFromRaw(uint16_t raw);

class AdcDriver {
public:
    explicit AdcDriver(adc_unit_t unit, adc_channel_t channel);
    ~AdcDriver();

    AdcDriver(const AdcDriver&) = delete;
    AdcDriver& operator=(const AdcDriver&) = delete;

    [[nodiscard]] domain::Result<uint16_t, domain::SensorError> readRaw();
    [[nodiscard]] int16_t calibratedMv();

private:
    adc_oneshot_unit_handle_t handle_{nullptr};
    adc_channel_t channel_;
    std::array<uint16_t, 64> buf_{};
    size_t count_{0};
};

} // namespace ecotiter::infrastructure::drivers
