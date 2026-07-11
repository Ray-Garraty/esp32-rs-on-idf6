#include "application/dispatch.hpp"

#include "application/command.hpp"
#include "infrastructure/storage/nvs.hpp"
#include "application/handlers/burette_ops.hpp"
#include "application/handlers/burette_cal.hpp"
#include "application/handlers/sensors.hpp"
#include "application/handlers/serial.hpp"
#include "application/handlers/system.hpp"
#include "application/handlers/valve.hpp"
#include "domain/calibration.hpp"

namespace ecotiter::application {

namespace {
auto readCal = infrastructure::storage::calibrationRead;
auto writeCal = infrastructure::storage::calibrationWrite;
void stubAdcCalRead(uint16_t& a, int16_t& b) {
  infrastructure::storage::adcCalibrationRead(a, b);
}
std::expected<void, domain::ResourceError> stubAdcCalWrite(uint16_t a, int16_t b) {
  return infrastructure::storage::adcCalibrationWrite(a, b);
}
uint16_t stubSampleRead() { return 0; }
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
      return burette_ops::handleDoseVolume(cmd.volume, cmd.speedMlMin);
    case CommandType::Rinse:
      return burette_ops::handleRinse(
          cmd.volume ? std::optional<uint32_t>{
              static_cast<uint32_t>(cmd.volume->value)} : std::nullopt);
    case CommandType::Stop:
      return burette_ops::handleStop();
    case CommandType::MoveToStop:
      return burette_ops::handleCalRun(
          std::optional<std::string_view>{"speed"}, std::nullopt, std::nullopt);
    case CommandType::EmergencyStop:
      return burette_ops::handleEmergencyStop();
    case CommandType::GetStatus:
      // Status requires current state — returns generic stub
      return burette_ops::handleGetStatus(
          domain::BuretteState::Idle, 0,
          domain::ValvePosition::Input, 0.0f,
          domain::Direction::LiqIn, domain::DEFAULT_SPEED_HZ,
          domain::DEFAULT_ACCEL_HZ_PER_S, domain::DEFAULT_VOLUME_ML);
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
      return burette_cal::handleGetCalibration(readCal);
    case CommandType::CalCalcVolume:
      return burette_cal::handleCalcVolume(
          cmd.steps, cmd.massG, cmd.temperature, cmd.pressure, readCal);
    case CommandType::CalCalcSpeed:
      return burette_cal::handleCalcSpeed(
          cmd.measurements.freqs, cmd.measurements.speeds,
          cmd.measurements.count, readCal);
    case CommandType::CalSave:
      return burette_cal::handleSaveCalibration(
          cmd.volume ? std::optional<float>{cmd.volume->value} : std::nullopt,
          cmd.targetVolume ? std::optional<float>{cmd.targetVolume->value} : std::nullopt,
          writeCal);
    case CommandType::CalReset:
      return burette_cal::handleResetCalibration(writeCal);
    case CommandType::CalRun:
      return burette_ops::handleCalRun(
          cmd.mode ? std::optional<std::string_view>{*cmd.mode} : std::nullopt,
          cmd.freqHz,
          cmd.speedMlMin);
    case CommandType::CalGetResult:
      return burette_cal::handleGetCalResult(readCal);
    case CommandType::CalRunSpeedSeq:
      return burette_cal::handleRunCalibration(
          cmd.freqsArray, cmd.freqsCount,
          cmd.speedMlMin.value_or(20.0f));

    // --- Sensors ---
    case CommandType::TempRead:
      return sensors::handleReadTemperature(0);
    case CommandType::AdcCalGet:
      return sensors::handleAdcCalGet(stubAdcCalRead);
    case CommandType::AdcCalSave:
      return sensors::handleAdcCalSave(std::nullopt, std::nullopt, stubAdcCalWrite);
    case CommandType::AdcCalMeasure:
      return sensors::handleAdcCalMeasure(cmd.refMv, stubSampleRead, stubAdcCalWrite);
    case CommandType::AdcCalCompute:
      return sensors::handleAdcCalCompute(stubAdcCalWrite);
    case CommandType::AdcCalReset:
      return sensors::handleAdcCalReset(stubAdcCalWrite);
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
          domain::Direction::LiqIn, domain::DEFAULT_SPEED_HZ,
          domain::DEFAULT_ACCEL_HZ_PER_S, domain::DEFAULT_VOLUME_ML);
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
