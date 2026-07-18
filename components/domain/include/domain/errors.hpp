#pragma once

#include <cstdint>
#include <expected>
#include <string_view>

namespace ecotiter::domain
{

enum class StepperError : uint8_t
{
    InitFailed,
    Rmt,
    LimitSwitchTriggered,
    LimitSwitchReached,
    Timeout
};

enum class SensorError : uint8_t
{
    AdcReadFailed,
    TempSensorNotDetected,
    TempReadGlitch
};

enum class NetworkError : uint8_t
{
    WifiConnectionFailed
};

enum class ProtocolError : uint8_t
{
    InvalidJson,
    UnknownCommand,
    MissingParam
};

enum class StateError : uint8_t
{
    Busy,
    InvalidTransition,
    AlreadyRunning
};

enum class ResourceError : uint8_t
{
    NvsOpenFailed,
    OutOfMemory
};

enum class AppError : uint8_t
{
    Protocol,
    State,
    Resource
};

template <typename T, typename E>
using Result = std::expected<T, E>;

} // namespace ecotiter::domain
