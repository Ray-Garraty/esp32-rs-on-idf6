#include "application/dispatch.hpp"

#include "application/command.hpp"
#include "application/handlers/burette_ops.hpp"
#include "application/handlers/burette_cal.hpp"
#include "application/handlers/sensors.hpp"
#include "application/handlers/serial.hpp"
#include "application/handlers/system.hpp"
#include "application/handlers/valve.hpp"
#include "domain/calibration.hpp"

namespace ecotiter::application {

// Stub callbacks — wired at integration time (Step 6).
namespace {
std::expected<domain::CalibrationData, domain::ResourceError> stubReadCal() {
  return std::unexpected(domain::ResourceError::NvsOpenFailed);
}
std::expected<void, domain::ResourceError> stubWriteCal(
    const domain::CalibrationData&) {
  return std::unexpected(domain::ResourceError::NvsOpenFailed);
}
void stubAdcCalRead(uint16_t&, int16_t&) {}
std::expected<void, domain::ResourceError> stubAdcCalWrite(uint16_t, int16_t) {
  return std::unexpected(domain::ResourceError::NvsOpenFailed);
}
} // anonymous namespace

std::expected<CommandResponse, domain::AppError> dispatch(
    const Command& cmd) {

  using namespace handlers;

  switch (cmd.type) {

    // --- Burette operations ---
    case CommandType::Fill:
      return burette_ops::handleFill();
    case CommandType::Empty:
      return burette_ops::handleEmpty();
    case CommandType::DoseVolume:
      return burette_ops::handleDoseVolume(cmd.volume);
    case CommandType::Rinse:
      return burette_ops::handleRinse();
    case CommandType::Stop:
      return burette_ops::handleStop();
    case CommandType::EmergencyStop:
      return burette_ops::handleEmergencyStop();
    case CommandType::GetStatus:
      // Status requires current state — returns generic stub
      return burette_ops::handleGetStatus(
          domain::BuretteState::Idle, 0,
          domain::ValvePosition::Input, 0.0f,
          domain::Direction::Cw, 1000, 500, 50.0f);
    case CommandType::MoveSteps:
      return burette_ops::handleMoveSteps(cmd.steps);
    case CommandType::SetDirection:
      return burette_ops::handleSetDirection(cmd.direction);
    case CommandType::SetSpeed:
      return burette_ops::handleSetSpeed(cmd.speed);
    case CommandType::SetAccel:
      return burette_ops::handleSetAccel(cmd.accel);
    case CommandType::SetVolume:
      return burette_ops::handleSetVolume(cmd.targetVolume);
    case CommandType::ConfigMove:
      return burette_ops::handleConfigMove(cmd.configMoveSpeed, cmd.configMoveAccel);
    case CommandType::ConfigHome:
      return burette_ops::handleConfigHome(cmd.configHomeSpeed);
    case CommandType::ConfigSensor:
      return burette_ops::handleConfigSensor(cmd.configSensorValue);

    // --- Calibration ---
    case CommandType::CalGet:
      return burette_cal::handleGetCalibration(stubReadCal);
    case CommandType::CalCalcVolume:
      return burette_cal::handleCalcVolume(cmd.steps, stubReadCal);
    case CommandType::CalCalcSpeed:
      return burette_cal::handleCalcSpeed(cmd.speed, stubReadCal);
    case CommandType::CalSave:
      return burette_cal::handleSaveCalibration(
          cmd.volume ? std::optional<float>{cmd.volume->value} : std::nullopt,
          cmd.targetVolume ? std::optional<float>{cmd.targetVolume->value} : std::nullopt,
          stubWriteCal);
    case CommandType::CalReset:
      return burette_cal::handleResetCalibration(stubWriteCal);
    case CommandType::CalRun:
      return burette_cal::handleRunCalibration();
    case CommandType::CalGetResult:
      return burette_cal::handleGetCalResult(stubReadCal);

    // --- Sensors ---
    case CommandType::TempRead:
      return sensors::handleReadTemperature(0);
    case CommandType::AdcCalGet:
      return sensors::handleAdcCalGet(stubAdcCalRead);
    case CommandType::AdcCalSave:
      return sensors::handleAdcCalSave(std::nullopt, std::nullopt, stubAdcCalWrite);
    case CommandType::StallGuardGet:
      return sensors::handleStallGuardGet(0);
    case CommandType::StallGuardSetThreshold:
      return sensors::handleStallGuardSetThreshold(cmd.sgThreshold);

    // --- Valve ---
    case CommandType::ValveSetPosition:
      return valve::handleSetPosition(cmd.valvePos);
    case CommandType::ValveGetState:
      return valve::handleGetState(domain::ValvePosition::Input);

    // --- System ---
    case CommandType::SystemGetStatus:
      return system::handleGetStatus(
          domain::BuretteState::Idle, 0,
          domain::ValvePosition::Input, 0.0f,
          domain::Direction::Cw, 1000, 500, 50.0f);
    case CommandType::SystemGetFormattedLogs:
      return system::handleGetFormattedLogs();
    case CommandType::SystemReadLog:
      return system::handleReadLog();
    case CommandType::SystemReboot:
      return system::handleReboot();
    case CommandType::SystemFirmwareVersion:
      return system::handleFirmwareVersion(std::nullopt);

    // --- Serial ---
    case CommandType::SerialPing:
      return serial::handlePing();
  }

  return std::unexpected(domain::AppError::Protocol);
}

} // namespace ecotiter::application
