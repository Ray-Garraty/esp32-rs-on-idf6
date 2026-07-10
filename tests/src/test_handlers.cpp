#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>

#include "application/command.hpp"
#include "application/handlers/serial.hpp"
#include "application/handlers/burette_ops.hpp"
#include "application/handlers/burette_cal.hpp"
#include "application/handlers/sensors.hpp"
#include "application/handlers/system.hpp"
#include "application/handlers/valve.hpp"

using namespace ecotiter::application;
using namespace ecotiter::application::handlers;
using namespace ecotiter::domain;

static constexpr float TOLERANCE = 0.01f;
static bool approx(float a, float b) { return std::fabs(a - b) < TOLERANCE; }

// --- serial ---

TEST_CASE("handler: serial.ping returns pong", "[handlers][serial]") {
  auto rsp = serial::handlePing();
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
  std::string_view sv(rsp->body.data(), rsp->bodySize);
  REQUIRE(sv.find("serial.ping") != std::string_view::npos);
  REQUIRE(sv.find("pong") != std::string_view::npos);
}

// --- burette_ops ---

TEST_CASE("handler: fill returns AckThen", "[handlers][burette]") {
  auto rsp = burette_ops::handleFill();
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::AckThen);
}

TEST_CASE("handler: empty returns AckThen", "[handlers][burette]") {
  auto rsp = burette_ops::handleEmpty();
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::AckThen);
}

TEST_CASE("handler: doseVolume with param returns AckThen", "[handlers][burette]") {
  auto rsp = burette_ops::handleDoseVolume(Ml{10.0f});
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::AckThen);
}

TEST_CASE("handler: doseVolume missing param returns error", "[handlers][burette]") {
  auto rsp = burette_ops::handleDoseVolume(std::nullopt);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Error);
}

TEST_CASE("handler: rinse cycles required", "[handlers][burette]") {
  auto rsp = burette_ops::handleRinse();
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Error);
}

TEST_CASE("handler: rinse with cycles returns AckThen", "[handlers][burette]") {
  auto rsp = burette_ops::handleRinse(3);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::AckThen);
}

TEST_CASE("handler: setDirection liq_in", "[handlers][burette]") {
  auto rsp = burette_ops::handleSetDirection(Direction::LiqIn);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::AckThen);
}

TEST_CASE("handler: setDirection missing param returns error", "[handlers][burette]") {
  auto rsp = burette_ops::handleSetDirection(std::nullopt);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Error);
}

TEST_CASE("handler: setSpeed", "[handlers][burette]") {
  auto rsp = burette_ops::handleSetSpeed(1500u);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::AckThen);
}

TEST_CASE("handler: setAccel", "[handlers][burette]") {
  auto rsp = burette_ops::handleSetAccel(200u);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::AckThen);
}

TEST_CASE("handler: setVolume", "[handlers][burette]") {
  auto rsp = burette_ops::handleSetVolume(Ml{25.0f});
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
  std::string_view sv(rsp->body.data(), rsp->bodySize);
  REQUIRE(sv.find("\"volume\":25.0") != std::string_view::npos);
}

TEST_CASE("handler: setVolume missing param returns error", "[handlers][burette]") {
  auto rsp = burette_ops::handleSetVolume(std::nullopt);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Error);
}

TEST_CASE("handler: moveSteps", "[handlers][burette]") {
  auto rsp = burette_ops::handleMoveSteps(Steps{500});
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::AckThen);
}

TEST_CASE("handler: moveSteps missing param returns error", "[handlers][burette]") {
  auto rsp = burette_ops::handleMoveSteps(std::nullopt);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Error);
}

TEST_CASE("handler: getStatus returns correct fields", "[handlers][burette]") {
  auto rsp = burette_ops::handleGetStatus(
      BuretteState::Idle, 2530, ValvePosition::Input,
      150.0f, Direction::LiqIn, 1000, 500, 50.0f);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
  std::string_view sv(rsp->body.data(), rsp->bodySize);
  REQUIRE(sv.find("idle") != std::string_view::npos);
  REQUIRE(sv.find("25.3") != std::string_view::npos); // 2530/100 = 25.3C
  REQUIRE(sv.find("input") != std::string_view::npos);
  REQUIRE(sv.find("150.0") != std::string_view::npos);
}

TEST_CASE("handler: stop", "[handlers][burette]") {
  auto rsp = burette_ops::handleStop();
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::AckThen);
}

TEST_CASE("handler: emergencyStop", "[handlers][burette]") {
  auto rsp = burette_ops::handleEmergencyStop();
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::AckThen);
}

TEST_CASE("handler: configMove", "[handlers][burette]") {
  auto rsp = burette_ops::handleConfigMove(2000u, 300u);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
  std::string_view sv(rsp->body.data(), rsp->bodySize);
  REQUIRE(sv.find("configMove") != std::string_view::npos);
  REQUIRE(sv.find("2000") != std::string_view::npos);
  REQUIRE(sv.find("300") != std::string_view::npos);
}

TEST_CASE("handler: configHome", "[handlers][burette]") {
  auto rsp = burette_ops::handleConfigHome(500u);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
  std::string_view sv(rsp->body.data(), rsp->bodySize);
  REQUIRE(sv.find("configHome") != std::string_view::npos);
  REQUIRE(sv.find("500") != std::string_view::npos);
}

TEST_CASE("handler: configSensor", "[handlers][burette]") {
  auto rsp = burette_ops::handleConfigSensor(42u);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
  std::string_view sv(rsp->body.data(), rsp->bodySize);
  REQUIRE(sv.find("configSensor") != std::string_view::npos);
  REQUIRE(sv.find("42") != std::string_view::npos);
}

// --- burette_cal ---

TEST_CASE("handler: cal.get returns error when NVS unavailable", "[handlers][cal]") {
  auto stubFail = []() -> std::expected<CalibrationData, ResourceError> {
    return std::unexpected(ResourceError::NvsOpenFailed);
  };
  auto rsp = burette_cal::handleGetCalibration(stubFail);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
  std::string_view sv(rsp->body.data(), rsp->bodySize);
  REQUIRE(sv.find("nvs_unavailable") != std::string_view::npos);
}

static bool gCalSaveCalled = false;
static bool gCalResetCalled = false;
static CalibrationData gSavedCal{};
static CalibrationData gResetCal{};

static std::expected<void, ResourceError> testSaveCalCb(
    const CalibrationData& cal) {
  gCalSaveCalled = true;
  gSavedCal = cal;
  return {};
}

static std::expected<void, ResourceError> testResetCalCb(
    const CalibrationData& cal) {
  gCalResetCalled = true;
  gResetCal = cal;
  return {};
}

TEST_CASE("handler: cal.save with write callback", "[handlers][cal]") {
  gCalSaveCalled = false;
  auto rsp = burette_cal::handleSaveCalibration(1500.0f, 25.0f, testSaveCalCb);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
  REQUIRE(gCalSaveCalled);
  REQUIRE(approx(gSavedCal.stepsPerMl, 1500.0f));
  REQUIRE(approx(gSavedCal.nominalVolumeMl, 25.0f));
  std::string_view sv(rsp->body.data(), rsp->bodySize);
  REQUIRE(sv.find("cal.save") != std::string_view::npos);
}

TEST_CASE("handler: cal.reset restores defaults", "[handlers][cal]") {
  gCalResetCalled = false;
  auto rsp = burette_cal::handleResetCalibration(testResetCalCb);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
  REQUIRE(gCalResetCalled);
  REQUIRE(approx(gResetCal.stepsPerMl, 7730.0f));
  REQUIRE(approx(gResetCal.nominalVolumeMl, 8.14f));
}

TEST_CASE("handler: cal.run returns AckThen", "[handlers][cal]") {
  auto rsp = burette_cal::handleRunCalibration();
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::AckThen);
}

static auto stubCalRead = []() -> std::expected<CalibrationData, ResourceError> {
  CalibrationData c{};
  c.stepsPerMl = 7730.0f;
  c.nominalVolumeMl = 8.14f;
  c.speedCoeff = 0.03052f;
  c.minFreqHz = 30;
  c.maxFreqHz = 3000;
  return c;
};

TEST_CASE("handler: cal.calcVolume with valid mass returns z_factor", "[handlers][cal]") {
  auto rsp = burette_cal::handleCalcVolume(
      std::nullopt, 10.0f, std::nullopt, std::nullopt, stubCalRead);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
  std::string_view sv(rsp->body.data(), rsp->bodySize);
  REQUIRE(sv.find("cal.calcVolume") != std::string_view::npos);
  REQUIRE(sv.find("z_factor") != std::string_view::npos);
  REQUIRE(sv.find("actual_volume_ml") != std::string_view::npos);
  REQUIRE(sv.find("new_steps_per_ml") != std::string_view::npos);
  REQUIRE(sv.find("relative_error_pct") != std::string_view::npos);
}

TEST_CASE("handler: cal.calcVolume missing mass_g returns error", "[handlers][cal]") {
  auto rsp = burette_cal::handleCalcVolume(
      std::nullopt, std::nullopt, std::nullopt, std::nullopt, stubCalRead);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Error);
  std::string_view sv(rsp->body.data(), rsp->bodySize);
  REQUIRE(sv.find("mass_g") != std::string_view::npos);
}

TEST_CASE("handler: cal.calcVolume mass_g <= 0 returns error", "[handlers][cal]") {
  auto rsp = burette_cal::handleCalcVolume(
      std::nullopt, 0.0f, std::nullopt, std::nullopt, stubCalRead);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Error);
}

TEST_CASE("handler: cal.calcVolume with custom temp/pressure", "[handlers][cal]") {
  auto rsp = burette_cal::handleCalcVolume(
      std::nullopt, 10.0f, 20.0f, 80.0f, stubCalRead);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
  std::string_view sv(rsp->body.data(), rsp->bodySize);
  REQUIRE(sv.find("z_factor") != std::string_view::npos);
}

TEST_CASE("handler: cal.calcSpeed valid measurements returns k", "[handlers][cal]") {
  float freqs[] = {100.0f, 500.0f, 1000.0f};
  float speeds[] = {3.052f, 15.26f, 30.52f};
  auto rsp = burette_cal::handleCalcSpeed(freqs, speeds, 3, stubCalRead);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
  std::string_view sv(rsp->body.data(), rsp->bodySize);
  REQUIRE(sv.find("cal.calcSpeed") != std::string_view::npos);
  REQUIRE(sv.find("k") != std::string_view::npos);
  REQUIRE(sv.find("r_squared") != std::string_view::npos);
  REQUIRE(sv.find("min_freq") != std::string_view::npos);
  REQUIRE(sv.find("max_freq") != std::string_view::npos);
}

TEST_CASE("handler: cal.calcSpeed less than 2 points returns error", "[handlers][cal]") {
  float freqs[] = {100.0f};
  float speeds[] = {3.052f};
  auto rsp = burette_cal::handleCalcSpeed(freqs, speeds, 1, stubCalRead);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Error);
}

TEST_CASE("handler: cal.calcSpeed with 2 points returns valid result", "[handlers][cal]") {
  float freqs[] = {100.0f, 200.0f};
  float speeds[] = {3.0f, 6.0f};
  auto rsp = burette_cal::handleCalcSpeed(freqs, speeds, 2, stubCalRead);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
  std::string_view sv(rsp->body.data(), rsp->bodySize);
  REQUIRE(sv.find("\"k\"") != std::string_view::npos);
  REQUIRE(sv.find("\"r_squared\"") != std::string_view::npos);
}

// --- sensors ---

TEST_CASE("handler: temperature.read formats celsius", "[handlers][sensors]") {
  auto rsp = sensors::handleReadTemperature(2530); // 25.30C
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
  std::string_view sv(rsp->body.data(), rsp->bodySize);
  REQUIRE(sv.find("25.3") != std::string_view::npos);
}

TEST_CASE("handler: temperature.read handles invalid temp", "[handlers][sensors]") {
  auto rsp = sensors::handleReadTemperature(-2147483648); // min int32
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
  std::string_view sv(rsp->body.data(), rsp->bodySize);
  REQUIRE(sv.find("0.0") != std::string_view::npos);
}

TEST_CASE("handler: stallGuard.get", "[handlers][sensors]") {
  auto rsp = sensors::handleStallGuardGet(64);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
  std::string_view sv(rsp->body.data(), rsp->bodySize);
  REQUIRE(sv.find("64") != std::string_view::npos);
}

TEST_CASE("handler: stallGuard.setThreshold", "[handlers][sensors]") {
  auto rsp = sensors::handleStallGuardSetThreshold(uint8_t{128});
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
  std::string_view sv(rsp->body.data(), rsp->bodySize);
  REQUIRE(sv.find("128") != std::string_view::npos);
}

TEST_CASE("handler: stallGuard.setThreshold missing param", "[handlers][sensors]") {
  auto rsp = sensors::handleStallGuardSetThreshold(std::nullopt);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Error);
}

TEST_CASE("handler: adc.cal.get", "[handlers][sensors]") {
  auto stubRead = [](uint16_t& a, int16_t& b) {
    a = 1200;
    b = 5;
  };
  auto rsp = sensors::handleAdcCalGet(stubRead);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
  std::string_view sv(rsp->body.data(), rsp->bodySize);
  REQUIRE(sv.find("1200") != std::string_view::npos);
  REQUIRE(sv.find("5") != std::string_view::npos);
}

// --- valve ---

TEST_CASE("handler: valve.setPosition output", "[handlers][valve]") {
  auto rsp = valve::handleSetPosition(ValvePosition::Output);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
  std::string_view sv(rsp->body.data(), rsp->bodySize);
  REQUIRE(sv.find("output") != std::string_view::npos);
}

TEST_CASE("handler: valve.setPosition input", "[handlers][valve]") {
  auto rsp = valve::handleSetPosition(ValvePosition::Input);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
  std::string_view sv(rsp->body.data(), rsp->bodySize);
  REQUIRE(sv.find("input") != std::string_view::npos);
}

TEST_CASE("handler: valve.setPosition missing param", "[handlers][valve]") {
  auto rsp = valve::handleSetPosition(std::nullopt);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Error);
}

TEST_CASE("handler: valve.getState input", "[handlers][valve]") {
  auto rsp = valve::handleGetState(ValvePosition::Input);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
  std::string_view sv(rsp->body.data(), rsp->bodySize);
  REQUIRE(sv.find("input") != std::string_view::npos);
}

TEST_CASE("handler: valve.getState output", "[handlers][valve]") {
  auto rsp = valve::handleGetState(ValvePosition::Output);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
  std::string_view sv(rsp->body.data(), rsp->bodySize);
  REQUIRE(sv.find("output") != std::string_view::npos);
}

// --- system ---

TEST_CASE("handler: system.getStatus returns valid JSON", "[handlers][system]") {
  auto rsp = system::handleGetStatus(
      BuretteState::Dosing, 0, ValvePosition::Output,
      100.0f, Direction::LiqOut, 2000, 400, 25.0f);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
  std::string_view sv(rsp->body.data(), rsp->bodySize);
  REQUIRE(sv.find("dosing") != std::string_view::npos);
  REQUIRE(sv.find("output") != std::string_view::npos);
  REQUIRE(sv.find("liq_out") != std::string_view::npos);
}

TEST_CASE("handler: system.firmwareVersion default", "[handlers][system]") {
  auto rsp = system::handleFirmwareVersion(std::nullopt);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
  std::string_view sv(rsp->body.data(), rsp->bodySize);
  REQUIRE(sv.find("0.1.0") != std::string_view::npos);
}

TEST_CASE("handler: system.firmwareVersion custom", "[handlers][system]") {
  auto rsp = system::handleFirmwareVersion(std::string_view("2.0.0"));
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
  std::string_view sv(rsp->body.data(), rsp->bodySize);
  REQUIRE(sv.find("2.0.0") != std::string_view::npos);
}

TEST_CASE("handler: system.reboot returns AckThen", "[handlers][system]") {
  auto rsp = system::handleReboot();
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::AckThen);
}
