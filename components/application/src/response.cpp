#include "application/response.hpp"

#include <cstdio>

#include "domain/calibration.hpp"
#include "infrastructure/storage/nvs.hpp"
#include "infrastructure/motor_task.hpp"

namespace ecotiter::application {

size_t formatSmResult(
    domain::memory::ResponseBuffer& buf,
    uint64_t resultId,
    const infrastructure::SmResult& smResult)
{
    size_t off = 0;

    if (smResult.type == infrastructure::SmResult::Type::None && smResult.stepsTaken > 0)
    {
        auto cal = infrastructure::storage::calibrationRead();
        float volDispensed = cal
            ? static_cast<float>(smResult.stepsTaken) / cal->stepsPerMl
            : 0.0f;
        off = static_cast<size_t>(
            std::snprintf(buf.data(), buf.size(),
                R"({"id":%llu,"status":"ok","data":{"volume_dispensed_ml":%.2f}})",
                static_cast<unsigned long long>(resultId),
                static_cast<double>(volDispensed)));
    }
    else if (smResult.type == infrastructure::SmResult::Type::RinseComplete)
    {
        off = static_cast<size_t>(
            std::snprintf(buf.data(), buf.size(),
                R"({"id":%llu,"status":"ok","data":{"cycles_completed":1}})",
                static_cast<unsigned long long>(resultId)));
    }
    else if (smResult.type == infrastructure::SmResult::Type::CalDoseComplete)
    {
        auto cal = infrastructure::storage::calibrationRead();
        float volDispensed = cal
            ? static_cast<float>(smResult.stepsTaken) / cal->stepsPerMl
            : 0.0f;
        off = static_cast<size_t>(
            std::snprintf(buf.data(), buf.size(),
                R"({"id":%llu,"status":"ok","data":{"volume_dispensed_ml":%.2f}})",
                static_cast<unsigned long long>(resultId),
                static_cast<double>(volDispensed)));
    }
    else if (smResult.type == infrastructure::SmResult::Type::CalSpeedComplete)
    {
        off = static_cast<size_t>(
            std::snprintf(buf.data(), buf.size(),
                R"({"id":%llu,"status":"ok","data":{"speed_ml_min":%.2f}})",
                static_cast<unsigned long long>(resultId),
                static_cast<double>(smResult.measuredSpeedMlMin)));
    }
    else if (smResult.type == infrastructure::SmResult::Type::CalSpeedSeqComplete)
    {
        off = static_cast<size_t>(
            std::snprintf(buf.data(), buf.size(),
                R"({"id":%llu,"status":"ok","data":{"status":"complete"}})",
                static_cast<unsigned long long>(resultId)));
    }
    else if (smResult.type == infrastructure::SmResult::Type::Error)
    {
        off = static_cast<size_t>(
            std::snprintf(buf.data(), buf.size(),
                R"({"id":%llu,"status":"error","message":"start_failed"})",
                static_cast<unsigned long long>(resultId)));
    }
    else
    {
        off = static_cast<size_t>(
            std::snprintf(buf.data(), buf.size(),
                R"({"id":%llu,"status":"ok","data":{"status":"complete"}})",
                static_cast<unsigned long long>(resultId)));
    }

    return off;
}

} // namespace ecotiter::application
