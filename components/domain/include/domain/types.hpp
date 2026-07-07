#pragma once

#include <cstdint>
#include <compare>

namespace ecotiter::domain {

struct Steps {
    int32_t value;
    constexpr auto operator<=>(const Steps&) const = default;
    constexpr Steps operator+(Steps other) const { return {value + other.value}; }
    constexpr Steps operator-(Steps other) const { return {value - other.value}; }
    constexpr Steps& operator+=(Steps other) { value += other.value; return *this; }
    constexpr Steps& operator-=(Steps other) { value -= other.value; return *this; }
};

struct Hz {
    uint32_t value;
    constexpr auto operator<=>(const Hz&) const = default;
};

struct Ml {
    float value;
    constexpr auto operator<=>(const Ml&) const = default;
};

struct MlMin {
    float value;
    constexpr auto operator<=>(const MlMin&) const = default;
};

enum class Direction : uint8_t { Cw, Ccw };

enum class ValvePosition : uint8_t { Input, Output };
enum class TransportMode : uint8_t { UsbActive, BleAdvertising, BleConnected };
enum class LimitSwitchId : uint8_t { Full, Home };

// GR-6: stack budget constants
inline constexpr size_t MOTOR_THREAD_STACK = 16384;
inline constexpr size_t MAIN_TASK_STACK = 32768;
inline constexpr size_t NET_OWNER_STACK = 16384;
inline constexpr size_t TEMP_THREAD_STACK = 16384;
inline constexpr size_t BLE_NOTIFY_STACK = 8192;
inline constexpr size_t HTTP_SERVER_STACK = 12288;

} // namespace ecotiter::domain
