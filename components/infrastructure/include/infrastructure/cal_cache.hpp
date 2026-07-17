#pragma once

#include <atomic>
#include "domain/calibration.hpp"

namespace ecotiter::infrastructure {
inline std::atomic<domain::CalibrationData*> gCalCache{nullptr};
} // namespace ecotiter::infrastructure
