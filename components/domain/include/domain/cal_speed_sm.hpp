#pragma once

#include <cstdint>

namespace ecotiter::domain::sm
{

enum class CalSpeedAction
{
    FillToLimit,
    EmptyToLimit,
    Complete,
    Error,
    SettleValve
};

struct CalSpeedSingleSm
{
    enum class Phase
    {
        Idle,
        Filling,
        Emptying,
        Done
    } phase;
    uint32_t elapsedMs;
    float measuredSpeedMlMin;
    bool started;

    void start() noexcept
    {
        phase = Phase::Idle;
        elapsedMs = 0;
        measuredSpeedMlMin = 0.0f;
        started = true;
    }

    CalSpeedAction onStart(float currentVolumeMl, float nominalVolumeMl) noexcept
    {
        if (!started)
            return CalSpeedAction::Error;
        phase = Phase::Filling;
        if (currentVolumeMl < nominalVolumeMl - 0.1f)
        {
            return CalSpeedAction::FillToLimit;
        }
        phase = Phase::Emptying;
        return CalSpeedAction::EmptyToLimit;
    }

    CalSpeedAction onFillComplete(uint32_t nowMs) noexcept
    {
        phase = Phase::Emptying;
        elapsedMs = nowMs;
        return CalSpeedAction::EmptyToLimit;
    }

    CalSpeedAction onEmptyComplete(uint32_t nowMs, float nominalVolumeMl) noexcept
    {
        uint32_t diff = nowMs - elapsedMs;
        if (diff == 0)
            diff = 1;
        elapsedMs = diff;
        measuredSpeedMlMin = nominalVolumeMl / (static_cast<float>(diff) / 60000.0f);
        phase = Phase::Done;
        started = false;
        return CalSpeedAction::Complete;
    }

    bool isComplete() const noexcept { return phase == Phase::Done; }
};

struct CalSpeedSeqSm
{
    enum class Phase
    {
        Idle,
        FillFirst,
        ValveSettleAfterFill,
        Empty,
        ValveSettleAfterEmpty,
        NextPoint,
        Done
    } phase;
    uint16_t freqs[3];
    float results[3];
    int seqIdx;
    uint32_t elapsedMs;
    uint32_t valveSettleStartMs;
    bool firstEver;
    bool started;

    static constexpr uint32_t VALVE_SWITCH_MS = 1000;
    static constexpr int CAL_SEQ_POINTS = 3;
    static constexpr float SPEED_SEQ_MULTIPLIER = 0.5f;

    void start(const uint16_t freqs_in[3]) noexcept
    {
        for (int i = 0; i < 3; i++)
        {
            freqs[i] = freqs_in[i];
            results[i] = 0.0f;
        }
        seqIdx = 0;
        elapsedMs = 0;
        valveSettleStartMs = 0;
        firstEver = true;
        phase = Phase::FillFirst;
        started = true;
    }

    CalSpeedAction onTick(uint32_t nowMs) noexcept
    {
        if (!started)
            return CalSpeedAction::Error;

        switch (phase)
        {
        case Phase::Idle:
            return CalSpeedAction::Complete;

        case Phase::FillFirst:
            phase = Phase::ValveSettleAfterFill;
            valveSettleStartMs = nowMs;
            return CalSpeedAction::SettleValve;

        case Phase::ValveSettleAfterFill:
            if (nowMs - valveSettleStartMs >= VALVE_SWITCH_MS)
            {
                phase = Phase::Empty;
                elapsedMs = nowMs;
                return CalSpeedAction::EmptyToLimit;
            }
            return CalSpeedAction::SettleValve;

        case Phase::Empty: {
            uint32_t diff = nowMs - elapsedMs;
            float speed = (diff > 0) ? (50.0f / (static_cast<float>(diff) / 60000.0f)) : 0.0f;
            results[seqIdx++] = speed;
            if (seqIdx >= CAL_SEQ_POINTS)
            {
                phase = Phase::Done;
                started = false;
                return CalSpeedAction::Complete;
            }
            phase = Phase::ValveSettleAfterEmpty;
            valveSettleStartMs = nowMs;
            return CalSpeedAction::SettleValve;
        }

        case Phase::ValveSettleAfterEmpty:
            if (nowMs - valveSettleStartMs >= VALVE_SWITCH_MS)
            {
                phase = Phase::FillFirst;
                return CalSpeedAction::FillToLimit;
            }
            return CalSpeedAction::SettleValve;

        case Phase::NextPoint:
        case Phase::Done:
            return CalSpeedAction::Complete;
        }
        return CalSpeedAction::Error;
    }

    bool isComplete() const noexcept { return phase == Phase::Done; }
};

} // namespace ecotiter::domain::sm
