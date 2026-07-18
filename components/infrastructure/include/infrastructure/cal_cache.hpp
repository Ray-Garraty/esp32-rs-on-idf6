#pragma once

#include "domain/atomic_owner.hpp"
#include "domain/calibration.hpp"

namespace ecotiter::infrastructure
{
inline domain::AtomicOwner<domain::CalibrationData> gCalCache{};
} // namespace ecotiter::infrastructure
