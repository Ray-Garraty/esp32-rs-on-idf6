#pragma once

#include "domain/calibration.hpp"
#include <atomic>

namespace ecotiter::infrastructure
{
inline std::atomic<domain::CalibrationData*> gCalCache{nullptr};
} // namespace ecotiter::infrastructure
