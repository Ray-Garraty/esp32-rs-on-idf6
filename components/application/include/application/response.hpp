#pragma once

#include <cstddef>
#include <cstdint>

#include "domain/memory.hpp"
#include "infrastructure/motor_task.hpp"

namespace ecotiter::application {

[[nodiscard]] size_t formatSmResult(
    domain::memory::ResponseBuffer& buf,
    uint64_t resultId,
    const infrastructure::SmResult& smResult);

} // namespace ecotiter::application
