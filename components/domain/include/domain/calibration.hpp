#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <expected>
#include <vector>
#include "domain/types.hpp"

namespace ecotiter::domain {

struct AdcCalibration {
    float a; // slope (mV per ADC count)
    float b; // offset (mV)
};

[[nodiscard]] inline float adcToMv(uint16_t raw, const AdcCalibration& cal) noexcept {
    return static_cast<float>(raw) * cal.a + cal.b;
}

struct CalibrationData {
    float stepsPerMl;
    float nominalVolumeMl;
    float speedCoeff;
    uint16_t minFreqHz;
    uint16_t maxFreqHz;

    static constexpr inline float kDefaultStepsPerMl = 7730.0f;
    static constexpr inline float kDefaultNominalVolumeMl = 8.14f;
    static constexpr inline float kDefaultSpeedCoeff = 0.03052f;
    static constexpr inline uint16_t kDefaultMinFreqHz = 30;
    static constexpr inline uint16_t kDefaultMaxFreqHz = 3000;
};

[[nodiscard]] inline Steps mlToSteps(Ml volume, const CalibrationData& cal) noexcept {
    return Steps{static_cast<int32_t>(volume.value * cal.stepsPerMl)};
}
[[nodiscard]] inline Ml stepsToMl(Steps steps, const CalibrationData& cal) noexcept {
    return Ml{static_cast<float>(steps.value) / cal.stepsPerMl};
}

[[nodiscard]] inline Hz speedMlMinToHz(MlMin speedMlMin, const CalibrationData& cal) noexcept {
    if (cal.speedCoeff < 0.000001f) return Hz{cal.minFreqHz};
    uint32_t freq = static_cast<uint32_t>(speedMlMin.value / cal.speedCoeff + 0.5f);
    if (freq < cal.minFreqHz) freq = cal.minFreqHz;
    if (freq > cal.maxFreqHz) freq = cal.maxFreqHz;
    return Hz{freq};
}

struct DosePlan {
    uint32_t totalCycles;
    float firstCycleVolMl;
    float remainingVolMl;
    bool singleCycle;
    bool needsFillFirst;
    bool valid;
};

[[nodiscard]] inline DosePlan planDose(float volumeMl, float currentVolumeMl, const CalibrationData& cal) noexcept {
    DosePlan plan{};
    if (volumeMl < 0.01f || volumeMl > 50.0f || cal.nominalVolumeMl < 0.001f) {
        plan.valid = false;
        return plan;
    }
    plan.valid = true;

    if (volumeMl > cal.nominalVolumeMl + 0.001f) {
        plan.totalCycles = static_cast<uint32_t>(volumeMl / cal.nominalVolumeMl + 0.999999f);
        plan.remainingVolMl = std::fmod(volumeMl, cal.nominalVolumeMl);
        if (plan.remainingVolMl < 0.01f) {
            plan.remainingVolMl = cal.nominalVolumeMl;
        } else {
            plan.totalCycles = static_cast<uint32_t>(volumeMl / cal.nominalVolumeMl) + 1;
            if (plan.totalCycles > 1) plan.totalCycles--;
        }
        plan.firstCycleVolMl = cal.nominalVolumeMl;
        plan.singleCycle = false;
    } else {
        plan.totalCycles = 1;
        plan.firstCycleVolMl = volumeMl;
        plan.remainingVolMl = 0;
        plan.singleCycle = true;
    }
    plan.needsFillFirst = (currentVolumeMl < plan.firstCycleVolMl - 0.001f);
    return plan;
}

struct VolumeTracker {
    float currentVolumeMl;

    void onFillComplete(const CalibrationData& cal) noexcept {
        currentVolumeMl = cal.nominalVolumeMl;
    }
    void onEmptyComplete() noexcept {
        currentVolumeMl = 0.0f;
    }
    void onDoseComplete(float dispensedVolMl) noexcept {
        currentVolumeMl -= dispensedVolMl;
        if (currentVolumeMl < 0.0f) currentVolumeMl = 0.0f;
    }
    void onStopDuringFill(float startVolMl, int32_t stepsTaken, const CalibrationData& cal) noexcept {
        currentVolumeMl = startVolMl + static_cast<float>(stepsTaken) / cal.stepsPerMl;
        if (currentVolumeMl > cal.nominalVolumeMl) currentVolumeMl = cal.nominalVolumeMl;
    }
    void onStopDuringEmpty(float startVolMl, int32_t stepsTaken, const CalibrationData& cal) noexcept {
        currentVolumeMl = startVolMl - static_cast<float>(stepsTaken) / cal.stepsPerMl;
        if (currentVolumeMl < 0.0f) currentVolumeMl = 0.0f;
    }
    void onHomingComplete(const CalibrationData& cal) noexcept {
        currentVolumeMl = cal.nominalVolumeMl;
    }
};

inline std::atomic<CalibrationData*> gCalCache{nullptr};

} // namespace ecotiter::domain
