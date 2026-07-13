#pragma once

#include <cstdint>
#include <memory_resource>
#include <vector>

namespace ecotiter::domain {

struct RampConfig {
    uint32_t accelSteps;
    uint32_t decelSteps;
    uint32_t minIntervalUs;  // full speed (shortest interval)
    uint32_t maxIntervalUs;  // start/stop (longest interval)
};

[[nodiscard]] std::pmr::vector<uint32_t> computeRamp(
    uint32_t totalSteps,
    const RampConfig& config,
    std::pmr::memory_resource* res = std::pmr::get_default_resource()
);

} // namespace ecotiter::domain
