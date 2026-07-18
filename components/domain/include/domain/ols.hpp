#pragma once

#include <cmath>
#include <cstddef>

namespace ecotiter::domain
{

struct SpeedCalResult
{
    float k;
    float rSquared;
};

[[nodiscard]] inline SpeedCalResult
calculateSpeedCalibration(const float* frequencies, const float* speeds, size_t count) noexcept
{
    SpeedCalResult result{0.0f, 0.0f};
    if (count < 2)
        return result;

    float sumF = 0.0f;
    float sumV = 0.0f;
    float sumFF = 0.0f;
    float sumFV = 0.0f;
    for (size_t i = 0; i < count; ++i)
    {
        sumF += frequencies[i];
        sumV += speeds[i];
        sumFF += frequencies[i] * frequencies[i];
        sumFV += frequencies[i] * speeds[i];
    }

    float n = static_cast<float>(count);
    float denom = sumFF - sumF * sumF / n;
    if (std::fabs(denom) < 0.000001f)
        return result;

    result.k = (sumFV - sumF * sumV / n) / denom;

    float meanV = sumV / n;
    float ssRes = 0.0f;
    float ssTot = 0.0f;
    for (size_t i = 0; i < count; ++i)
    {
        float pred = result.k * frequencies[i];
        float res = speeds[i] - pred;
        float dev = speeds[i] - meanV;
        ssRes += res * res;
        ssTot += dev * dev;
    }
    if (ssTot > 0.000001f)
    {
        result.rSquared = 1.0f - ssRes / ssTot;
    }

    return result;
}

} // namespace ecotiter::domain
