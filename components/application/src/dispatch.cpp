#include "application/dispatch.hpp"

#include "application/command.hpp"
#include "application/handlers/burette_cal.hpp"
#include "application/handlers/burette_ops.hpp"
#include "application/handlers/sensors.hpp"
#include "application/handlers/serial.hpp"
#include "application/handlers/system.hpp"
#include "application/handlers/valve.hpp"
#include "domain/calibration.hpp"
#include "infrastructure/storage/nvs.hpp"

namespace ecotiter::application
{

namespace
{
auto readCal = infrastructure::storage::calibrationRead;
auto writeCal = infrastructure::storage::calibrationWrite;
void stubAdcCalRead(uint16_t& a, int16_t& b)
{
    infrastructure::storage::adcCalibrationRead(a, b);
}
std::expected<void, domain::ResourceError> stubAdcCalWrite(uint16_t a, int16_t b)
{
    return infrastructure::storage::adcCalibrationWrite(a, b);
}
uint16_t (*s_adcSampleReadCb)() = nullptr;
uint16_t stubSampleRead()
{
    if (s_adcSampleReadCb)
        return s_adcSampleReadCb();
    return 0;
}

// Motor controller instance — set once during boot
IMotorController* s_motorController = nullptr;

} // anonymous namespace

void setAdcSampleReadCb(uint16_t (*cb)())
{
    s_adcSampleReadCb = cb;
}

void setMotorController(IMotorController* controller) noexcept
{
    s_motorController = controller;
}

IMotorController* getMotorController() noexcept
{
    return s_motorController;
}

std::expected<CommandResponse, domain::AppError> dispatch(const Command& cmd)
{

    using namespace handlers;

    // Helper to copy cmd.id into the response after dispatch
    auto withId = [&](std::expected<CommandResponse, domain::AppError>&& rsp)
        -> std::expected<CommandResponse, domain::AppError> {
        if (rsp)
        {
            rsp->id = cmd.id;
        }
        return std::move(rsp);
    };

    switch (cmd.type)
    {

    // --- Burette operations ---
    case CommandType::Fill:
        return withId(burette_ops::handleFill());
    case CommandType::Empty:
        return withId(burette_ops::handleEmpty());
    case CommandType::DoseVolume:
        return withId(burette_ops::handleDoseVolume(cmd.volume, cmd.speedMlMin));
    case CommandType::Rinse:
        return withId(burette_ops::handleRinse(
            cmd.volume ? std::optional<uint32_t>{static_cast<uint32_t>(cmd.volume->value)}
                       : std::nullopt));
    case CommandType::Stop:
        return withId(burette_ops::handleStop());
    case CommandType::MoveToStop:
        return withId(burette_ops::handleStop());
    case CommandType::EmergencyStop:
        return withId(burette_ops::handleEmergencyStop());
    case CommandType::GetStatus:
        return withId(burette_ops::handleGetStatus(
            domain::gBuretteState.load(std::memory_order_acquire),
            domain::gTempCX100.load(std::memory_order_acquire),
            domain::gValvePosition.load(std::memory_order_acquire),
            static_cast<float>(domain::gLastMv.load(std::memory_order_acquire)),
            domain::gDirection.load(std::memory_order_acquire),
            domain::gSpeed.load(std::memory_order_acquire),
            domain::gAccel.load(std::memory_order_acquire),
            domain::gVolumeMl.load(std::memory_order_acquire)));
    case CommandType::MoveSteps:
        return withId(burette_ops::handleMoveSteps(cmd.steps));
    case CommandType::SetDirection:
        return withId(burette_ops::handleSetDirection(cmd.direction));
    case CommandType::SetSpeed:
        return withId(burette_ops::handleSetSpeed(cmd.speed));
    case CommandType::SetAccel:
        return withId(burette_ops::handleSetAccel(cmd.accel));

    // --- Calibration ---
    case CommandType::CalGet:
        return withId(burette_cal::handleGetCalibration(readCal));
    case CommandType::CalCalcVolume:
        return withId(burette_cal::handleCalcVolume(cmd.steps, cmd.massG, cmd.temperature,
                                                    cmd.pressure, readCal));
    case CommandType::CalCalcSpeed:
        return withId(burette_cal::handleCalcSpeed(cmd.measurements.freqs, cmd.measurements.speeds,
                                                   cmd.measurements.count, readCal));
    case CommandType::CalSave:
        return withId(burette_cal::handleSaveCalibration(
            cmd.volume ? std::optional<float>{cmd.volume->value} : std::nullopt,
            cmd.targetVolume ? std::optional<float>{cmd.targetVolume->value} : std::nullopt,
            writeCal));
    case CommandType::CalReset:
        return withId(burette_cal::handleResetCalibration(writeCal));
    case CommandType::CalRun:
        return withId(burette_ops::handleCalRun(
            cmd.mode ? std::optional<std::string_view>{*cmd.mode} : std::nullopt, cmd.freqHz,
            cmd.speedMlMin));
    case CommandType::CalGetResult:
        return withId(burette_cal::handleGetCalResult(readCal));
    case CommandType::CalRunSpeedSeq:
        return withId(burette_cal::handleRunCalibration(cmd.freqsArray, cmd.freqsCount,
                                                        cmd.speedMlMin.value_or(20.0f)));

    // --- Sensors ---
    case CommandType::TempRead:
        return withId(sensors::handleReadTemperature(0));
    case CommandType::AdcCalGet:
        return withId(sensors::handleAdcCalGet(stubAdcCalRead));
    case CommandType::AdcCalSave:
        return withId(sensors::handleAdcCalSave(std::nullopt, std::nullopt, stubAdcCalWrite));
    case CommandType::AdcCalMeasure:
        return withId(sensors::handleAdcCalMeasure(cmd.refMv, stubSampleRead, stubAdcCalWrite));
    case CommandType::AdcCalCompute:
        return withId(sensors::handleAdcCalCompute(stubAdcCalWrite));
    case CommandType::AdcCalReset:
        return withId(sensors::handleAdcCalReset(stubAdcCalWrite));
    case CommandType::StallGuardGet:
        return withId(sensors::handleStallGuardGet(0));
    case CommandType::StallGuardSetThreshold:
        return withId(sensors::handleStallGuardSetThreshold(cmd.sgThreshold));

    // --- Valve ---
    case CommandType::ValveSetPosition:
        return withId(valve::handleSetPosition(cmd.valvePos));
    case CommandType::ValveGetState:
        return withId(
            valve::handleGetState(domain::gValvePosition.load(std::memory_order_acquire)));

    // --- System ---
    case CommandType::SystemGetStatus:
        return withId(system::handleGetStatus(
            domain::gBuretteState.load(std::memory_order_acquire),
            domain::gTempCX100.load(std::memory_order_acquire),
            domain::gValvePosition.load(std::memory_order_acquire),
            static_cast<float>(domain::gLastMv.load(std::memory_order_acquire)),
            domain::gDirection.load(std::memory_order_acquire),
            domain::gSpeed.load(std::memory_order_acquire),
            domain::gAccel.load(std::memory_order_acquire),
            domain::gVolumeMl.load(std::memory_order_acquire)));
    case CommandType::SystemGetFormattedLogs:
        return withId(system::handleGetFormattedLogs());
    case CommandType::SystemReadLog:
        return withId(system::handleReadLog());
    case CommandType::SystemFirmwareVersion:
        return withId(system::handleFirmwareVersion(std::nullopt));

    // --- Serial ---
    case CommandType::SerialPing:
        return withId(serial::handlePing());
    }

    return std::unexpected(domain::AppError::Protocol);
}

} // namespace ecotiter::application
