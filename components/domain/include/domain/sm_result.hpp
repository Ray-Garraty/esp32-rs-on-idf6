#pragma once

#include <cstdint>

namespace ecotiter::domain
{

/// Result payload produced by the state machine (motor task) and consumed
/// by application-layer formatters.
///
/// ABI-critical: layout must remain stable because FreeRTOS queues use
/// sizeof(SmResult) for message sizing. See static_asserts below.
struct SmResult
{
    enum class Type : uint8_t
    {
        None,
        RinseComplete,
        CalDoseComplete,
        CalSpeedComplete,
        CalSpeedSeqComplete,
        Error
    } type;
    int32_t stepsTaken;
    float measuredSpeedMlMin;
    float results[3];
    int resultCount;
};

// ABI safety: verify expected size (28 bytes) on ESP32-S3
//   offset  size  field
//   0       1     Type (uint8_t)
//   1-3     3     padding
//   4       4     stepsTaken (int32_t)
//   8       4     measuredSpeedMlMin (float)
//   12      12    results[3] (float[3])
//   24      4     resultCount (int)
//   total        28
static_assert(sizeof(SmResult) == 28,
              "SmResult size changed — ABI break! Update FreeRTOS queue sizes.");
static_assert(sizeof(SmResult::Type) == 1, "SmResult::Type must be uint8_t (1 byte)");

} // namespace ecotiter::domain
