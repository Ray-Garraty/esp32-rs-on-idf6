#include "domain/motion.hpp"
#include <cmath>

namespace ecotiter::domain {

std::pmr::vector<uint32_t> computeRamp(
    uint32_t totalSteps,
    const RampConfig& config,
    std::pmr::memory_resource* res)
{
    std::pmr::vector<uint32_t> ramp{res};

    if (totalSteps == 0) {
        return ramp;
    }

    uint32_t accel = config.accelSteps;
    uint32_t decel = config.decelSteps;

    if (accel + decel > totalSteps) {
        double scale = static_cast<double>(totalSteps) / (accel + decel);
        accel = static_cast<uint32_t>(accel * scale);
        decel = static_cast<uint32_t>(decel * scale);
        while (accel + decel > totalSteps && accel > 0) accel--;
        while (accel + decel > totalSteps && decel > 0) decel--;
    }

    uint32_t cruise = totalSteps - accel - decel;
    ramp.reserve(totalSteps);

    double diff = static_cast<double>(config.maxIntervalUs) - static_cast<double>(config.minIntervalUs);

    for (uint32_t i = 0; i < accel; i++) {
        double t = static_cast<double>(i) / accel;
        double val = static_cast<double>(config.maxIntervalUs) - diff * std::sqrt(t);
        ramp.push_back(static_cast<uint32_t>(val + 0.5));
    }

    for (uint32_t i = 0; i < cruise; i++) {
        ramp.push_back(config.minIntervalUs);
    }

    for (uint32_t i = 0; i < decel; i++) {
        double t = static_cast<double>(i) / decel;
        double val = static_cast<double>(config.minIntervalUs) + diff * std::sqrt(t);
        ramp.push_back(static_cast<uint32_t>(val + 0.5));
    }

    return ramp;
}

} // namespace ecotiter::domain
