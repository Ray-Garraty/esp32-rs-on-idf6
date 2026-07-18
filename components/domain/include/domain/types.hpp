#pragma once

#include <atomic>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace ecotiter::domain
{

struct Steps
{
    int32_t value;
    constexpr auto operator<=>(const Steps&) const = default;
    constexpr Steps operator+(Steps other) const { return {value + other.value}; }
    constexpr Steps operator-(Steps other) const { return {value - other.value}; }
    constexpr Steps& operator+=(Steps other)
    {
        value += other.value;
        return *this;
    }
    constexpr Steps& operator-=(Steps other)
    {
        value -= other.value;
        return *this;
    }
};

struct Hz
{
    uint32_t value;
    constexpr auto operator<=>(const Hz&) const = default;
};

struct Ml
{
    float value;
    constexpr auto operator<=>(const Ml&) const = default;
};

struct MlMin
{
    float value;
    constexpr auto operator<=>(const MlMin&) const = default;
};

enum class Direction : uint8_t
{
    LiqIn,
    LiqOut
};

enum class ValvePosition : uint8_t
{
    Input,
    Output
};
enum class TransportMode : uint8_t
{
    UsbActive,
    BleAdvertising,
    BleConnected
};

enum class BuretteState : uint8_t
{
    Idle,
    Homing,
    Filling,
    Emptying,
    Dosing,
    Rinsing,
    Stopping,
    Error
};

[[nodiscard]] inline const char* buretteStateStr(BuretteState s) noexcept
{
    switch (s)
    {
    case BuretteState::Idle:
        return "idle";
    case BuretteState::Homing:
        return "homing";
    case BuretteState::Filling:
        return "filling";
    case BuretteState::Emptying:
        return "emptying";
    case BuretteState::Dosing:
        return "dosing";
    case BuretteState::Rinsing:
        return "rinsing";
    case BuretteState::Stopping:
        return "stopping";
    case BuretteState::Error:
        return "error";
    }
    return "unknown";
}

enum class BuretteCommand : uint8_t
{
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
inline constexpr size_t NET_OWNER_STACK = 20480;
inline constexpr size_t LOG_WORKER_STACK = 16384;
inline constexpr size_t TEMP_THREAD_STACK = 16384;
inline constexpr size_t BLE_NOTIFY_STACK = 8192;
// Matches HttpServer::STACK_SIZE (both must be kept in sync)
inline constexpr size_t HTTP_SERVER_STACK = 16384;

// Default runtime values
inline constexpr uint32_t DEFAULT_SPEED_HZ = 1000;
inline constexpr uint32_t DEFAULT_ACCEL_HZ_PER_S = 500;
inline constexpr float DEFAULT_VOLUME_ML = 50.0f;

// Global hardware state atoms — published state for broadcasts and handlers
// (infrastructure layer owns the raw hardware globals in a different namespace)
inline std::atomic<int32_t> gTempCX100{-99999};
inline std::atomic<uint16_t> gLastMv{0};
inline std::atomic<ValvePosition> gValvePosition{ValvePosition::Input};
inline std::atomic<BuretteState> gBuretteState{BuretteState::Idle};
inline std::atomic<Direction> gDirection{Direction::LiqIn};
inline std::atomic<uint32_t> gSpeed{DEFAULT_SPEED_HZ};
inline std::atomic<uint32_t> gAccel{DEFAULT_ACCEL_HZ_PER_S};
inline std::atomic<float> gVolumeMl{DEFAULT_VOLUME_ML};
inline std::atomic<float> gSpeedMlMin{0.0f};
inline std::atomic<bool> gStopFull{false};
inline std::atomic<bool> gStopEmpty{false};
inline std::atomic<uint32_t> gDispensedSteps{0};
inline std::atomic<bool> gUsbHandshakeReceived{false};
inline std::atomic<bool> gBleError{false};
inline std::atomic<uint8_t> gStallGuardThreshold{0};
inline std::atomic<bool> gMotorIsMoving{false};

// Result delivery for serial/BLE — motor task pushes to gSmResultQueue
inline std::atomic<uint64_t> gLastCmdId{0};

// Homing completion flag — net_owner waits for this before starting WiFi AP
// (WiFi AP + active RMT homing cause interrupt storm on UNICORE, LL-044)
inline std::atomic<bool> gHomingDone{false};

// Boot progress tracking — diagnostic heartbeat for serial monitor
// Set once at each init step. If the device hangs, the last value is visible
// on the serial monitor without a working WDT (LL-032: spinlock deadlocks
// mask TWDT because interrupts are disabled).
enum class BootProgress : uint8_t
{
    Start,
    Nvs,
    BlackBox,
    StackMonitor,
    Serial,
    RtcWdt,
    BleInit,
    PhyWait,
    MotorTask,
    TempTask,
    NetOwner,
    Running
};

inline std::atomic<BootProgress> gBootProgress{BootProgress::Start};

} // namespace ecotiter::domain
