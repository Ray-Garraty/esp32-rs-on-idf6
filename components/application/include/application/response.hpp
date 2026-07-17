#pragma once

#include <cstddef>
#include <cstdint>

#include "domain/memory.hpp"
#include "domain/sm_result.hpp"

namespace ecotiter::application {

[[nodiscard]] size_t formatSmResult(
    domain::memory::ResponseBuffer& buf,
    uint64_t resultId,
    const domain::SmResult& smResult);

} // namespace ecotiter::application
