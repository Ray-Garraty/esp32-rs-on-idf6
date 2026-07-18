#pragma once

#include <cstdint>

namespace ecotiter::domain::sm
{

enum class CalDoseAction
{
    FillToLimit,
    EmptyToLimit,
    Complete,
    Error
};

struct CalDoseSm
{
    enum class Phase
    {
        Idle,
        Filling,
        Emptying,
        Done
    } phase;
    int32_t stepsBefore;
    int32_t stepsTaken;
    bool started;

    void start() noexcept
    {
        phase = Phase::Idle;
        stepsBefore = 0;
        stepsTaken = 0;
        started = true;
    }

    CalDoseAction onStart(float currentVolumeMl, float nominalVolumeMl) noexcept
    {
        if (!started)
            return CalDoseAction::Error;
        phase = Phase::Filling;
        if (currentVolumeMl < nominalVolumeMl - 0.1f)
        {
            return CalDoseAction::FillToLimit;
        }
        phase = Phase::Emptying;
        return CalDoseAction::EmptyToLimit;
    }

    CalDoseAction onFillComplete(int32_t position) noexcept
    {
        stepsBefore = position;
        phase = Phase::Emptying;
        return CalDoseAction::EmptyToLimit;
    }

    CalDoseAction onEmptyComplete(int32_t position) noexcept
    {
        int32_t diff = position - stepsBefore;
        stepsTaken = (diff > 0) ? diff : -diff;
        phase = Phase::Done;
        started = false;
        return CalDoseAction::Complete;
    }

    bool isComplete() const noexcept { return phase == Phase::Done; }
};

} // namespace ecotiter::domain::sm
