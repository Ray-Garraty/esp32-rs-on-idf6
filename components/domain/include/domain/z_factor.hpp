#pragma once

#include <cstddef>

namespace ecotiter::domain {

inline constexpr size_t Z_TABLE_ROWS = 31;
inline constexpr size_t Z_TABLE_COLS = 6;

inline constexpr float Z_TEMPS[Z_TABLE_ROWS] = {
    15.0f, 15.5f, 16.0f, 16.5f, 17.0f, 17.5f, 18.0f, 18.5f,
    19.0f, 19.5f, 20.0f, 20.5f, 21.0f, 21.5f, 22.0f, 22.5f,
    23.0f, 23.5f, 24.0f, 24.5f, 25.0f, 25.5f, 26.0f, 26.5f,
    27.0f, 27.5f, 28.0f, 28.5f, 29.0f, 29.5f, 30.0f
};

inline constexpr float Z_PRESSURES[Z_TABLE_COLS] = {80.0f, 85.3f, 90.7f, 96.0f, 101.3f, 106.7f};

inline constexpr float Z_TABLE[Z_TABLE_ROWS][Z_TABLE_COLS] = {
    {1.0018f, 1.0018f, 1.0019f, 1.0019f, 1.0020f, 1.0020f},
    {1.0018f, 1.0018f, 1.0019f, 1.0020f, 1.0020f, 1.0021f},
    {1.0019f, 1.0020f, 1.0020f, 1.0021f, 1.0022f, 1.0022f},
    {1.0020f, 1.0020f, 1.0021f, 1.0022f, 1.0022f, 1.0023f},
    {1.0021f, 1.0021f, 1.0022f, 1.0022f, 1.0023f, 1.0023f},
    {1.0022f, 1.0022f, 1.0023f, 1.0024f, 1.0024f, 1.0024f},
    {1.0022f, 1.0023f, 1.0024f, 1.0024f, 1.0025f, 1.0025f},
    {1.0023f, 1.0024f, 1.0025f, 1.0025f, 1.0026f, 1.0026f},
    {1.0024f, 1.0025f, 1.0025f, 1.0026f, 1.0027f, 1.0027f},
    {1.0025f, 1.0026f, 1.0026f, 1.0027f, 1.0028f, 1.0028f},
    {1.0026f, 1.0027f, 1.0027f, 1.0028f, 1.0029f, 1.0029f},
    {1.0027f, 1.0028f, 1.0028f, 1.0029f, 1.0030f, 1.0030f},
    {1.0028f, 1.0029f, 1.0031f, 1.0031f, 1.0032f, 1.0032f},
    {1.0030f, 1.0030f, 1.0032f, 1.0032f, 1.0033f, 1.0033f},
    {1.0031f, 1.0031f, 1.0033f, 1.0033f, 1.0034f, 1.0035f},
    {1.0032f, 1.0032f, 1.0034f, 1.0035f, 1.0035f, 1.0036f},
    {1.0033f, 1.0033f, 1.0035f, 1.0036f, 1.0036f, 1.0037f},
    {1.0034f, 1.0035f, 1.0036f, 1.0037f, 1.0038f, 1.0038f},
    {1.0035f, 1.0036f, 1.0037f, 1.0038f, 1.0039f, 1.0039f},
    {1.0037f, 1.0037f, 1.0038f, 1.0039f, 1.0040f, 1.0041f},
    {1.0038f, 1.0038f, 1.0039f, 1.0040f, 1.0041f, 1.0042f},
    {1.0039f, 1.0040f, 1.0041f, 1.0041f, 1.0042f, 1.0043f},
    {1.0040f, 1.0041f, 1.0042f, 1.0042f, 1.0043f, 1.0045f},
    {1.0042f, 1.0042f, 1.0043f, 1.0044f, 1.0045f, 1.0046f},
    {1.0043f, 1.0044f, 1.0044f, 1.0045f, 1.0048f, 1.0049f},
    {1.0046f, 1.0046f, 1.0047f, 1.0048f, 1.0048f, 1.0049f},
    {1.0046f, 1.0046f, 1.0047f, 1.0048f, 1.0049f, 1.0049f},
    {1.0047f, 1.0048f, 1.0048f, 1.0049f, 1.0050f, 1.0050f},
    {1.0049f, 1.0049f, 1.0050f, 1.0051f, 1.0051f, 1.0052f},
    {1.0050f, 1.0051f, 1.0051f, 1.0052f, 1.0053f, 1.0053f},
    {1.0052f, 1.0052f, 1.0053f, 1.0054f, 1.0054f, 1.0055f},
};

[[nodiscard]] inline float getZFactor(float temperature, float pressure) noexcept {
    if (temperature < Z_TEMPS[0]) temperature = Z_TEMPS[0];
    if (temperature > Z_TEMPS[Z_TABLE_ROWS - 1]) temperature = Z_TEMPS[Z_TABLE_ROWS - 1];
    if (pressure < Z_PRESSURES[0]) pressure = Z_PRESSURES[0];
    if (pressure > Z_PRESSURES[Z_TABLE_COLS - 1]) pressure = Z_PRESSURES[Z_TABLE_COLS - 1];

    size_t ti = 0;
    size_t pi = 0;
    for (size_t i = 0; i < Z_TABLE_ROWS - 1; ++i) {
        if (temperature >= Z_TEMPS[i]) ti = i;
    }
    for (size_t i = 0; i < Z_TABLE_COLS - 1; ++i) {
        if (pressure >= Z_PRESSURES[i]) pi = i;
    }

    float tFrac = (temperature - Z_TEMPS[ti]) / (Z_TEMPS[ti + 1] - Z_TEMPS[ti]);
    float pFrac = (pressure - Z_PRESSURES[pi]) / (Z_PRESSURES[pi + 1] - Z_PRESSURES[pi]);

    auto lerp = [](float a, float b, float t) noexcept -> float {
        return a + (b - a) * t;
    };

    float zT1 = lerp(Z_TABLE[ti][pi], Z_TABLE[ti][pi + 1], pFrac);
    float zT2 = lerp(Z_TABLE[ti + 1][pi], Z_TABLE[ti + 1][pi + 1], pFrac);
    return lerp(zT1, zT2, tFrac);
}

[[nodiscard]] inline float calculateNewStepsPerMl(float currentSpm, float targetVolMl, float actualVolMl) noexcept {
    if (actualVolMl < 0.0001f) return currentSpm;
    return currentSpm * targetVolMl / actualVolMl;
}

} // namespace ecotiter::domain
