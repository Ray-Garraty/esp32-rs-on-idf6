#pragma once

#include <atomic>
#include <cstddef>
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

enum class BuretteState : uint8_t {
    Idle,
    Homing,
    Filling,
    Emptying,
    Dosing,
    Rinsing,
    Stopping,
    Error
};

enum class BuretteCommand : uint8_t {
    Fill,
    Dose,
    Empty,
    Rinse,
    Stop,
    EmergencyStop,
    Reset
};

// GR-6: stack budget constants
inline constexpr size_t MOTOR_THREAD_STACK = 16384;
inline constexpr size_t MAIN_TASK_STACK = 32768;
inline constexpr size_t NET_OWNER_STACK = 16384;
inline constexpr size_t TEMP_THREAD_STACK = 16384;
inline constexpr size_t BLE_NOTIFY_STACK = 8192;
inline constexpr size_t HTTP_SERVER_STACK = 12288;

// Global hardware state atoms — published state for broadcasts and handlers
// (infrastructure layer owns the raw hardware globals in a different namespace)
inline std::atomic<int32_t> gTempCX100{-99999};
inline std::atomic<uint16_t> gLastMv{0};
inline std::atomic<ValvePosition> gValvePosition{ValvePosition::Input};
inline std::atomic<BuretteState> gBuretteState{BuretteState::Idle};
inline std::atomic<Direction> gDirection{Direction::Cw};
inline std::atomic<uint32_t> gSpeed{1000};
inline std::atomic<uint32_t> gAccel{500};
inline std::atomic<float> gVolumeMl{50.0f};
inline std::atomic<bool> gStopFull{false};
inline std::atomic<bool> gStopHome{false};
inline std::atomic<uint32_t> gDispensedSteps{0};

} // namespace ecotiter::domain
