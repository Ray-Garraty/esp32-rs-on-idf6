#pragma once

#include <cstdint>

namespace ecotiter::domain::sm
{

enum class RinseAction
{
    FillToLimit,
    EmptyToLimit,
    Complete,
    Error
};

struct RinseSm
{
    uint8_t totalCycles;
    uint8_t currentCycle;
    enum class Phase
    {
        PreFill,
        Emptying,
        Filling,
        Done
    } phase;
    bool started;

    void start(uint8_t cycles, float currentVolumeMl, float nominalVolumeMl) noexcept
    {
        totalCycles = cycles;
        currentCycle = 1;
        started = true;
        if (currentVolumeMl < nominalVolumeMl - 0.01f)
        {
            phase = Phase::PreFill;
        }
        else
        {
            phase = Phase::Emptying;
        }
    }

    RinseAction onMotorComplete(float currentVolumeMl, float nominalVolumeMl) noexcept
    {
        if (!started)
            return RinseAction::Error;
        switch (phase)
        {
        case Phase::PreFill:
            phase = Phase::Emptying;
            return RinseAction::EmptyToLimit;
        case Phase::Emptying:
            phase = Phase::Filling;
            return RinseAction::FillToLimit;
        case Phase::Filling: {
            currentCycle++;
            if (currentCycle > totalCycles)
            {
                phase = Phase::Done;
                started = false;
                return RinseAction::Complete;
            }
            return RinseAction::EmptyToLimit;
        }
        case Phase::Done:
            return RinseAction::Complete;
        }
        return RinseAction::Error;
    }

    bool isComplete() const noexcept { return phase == Phase::Done; }
};

} // namespace ecotiter::domain::sm
