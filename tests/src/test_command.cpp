#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>

#include "application/command.hpp"

using namespace ecotiter::application;
using namespace ecotiter::domain;

static constexpr float TOLERANCE = 0.01f;
static bool approx(float a, float b) { return std::fabs(a - b) < TOLERANCE; }

TEST_CASE("parseCommand: serial.ping", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"serial.ping"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::SerialPing);
}

TEST_CASE("parseCommand: fill", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"fill"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::Fill);
}

TEST_CASE("parseCommand: doseVolume with volume", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"doseVolume","volume":10.5})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::DoseVolume);
  REQUIRE(cmd->volume.has_value());
  REQUIRE(approx(cmd->volume->value, 10.5f));
}

TEST_CASE("parseCommand: setDirection cw", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"setDirection","direction":"cw"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::SetDirection);
  REQUIRE(cmd->direction.has_value());
  REQUIRE(*cmd->direction == Direction::Cw);
}

TEST_CASE("parseCommand: setDirection ccw", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"setDirection","direction":"ccw"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::SetDirection);
  REQUIRE(cmd->direction.has_value());
  REQUIRE(*cmd->direction == Direction::Ccw);
}

TEST_CASE("parseCommand: moveSteps", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"moveSteps","steps":500})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::MoveSteps);
  REQUIRE(cmd->steps.has_value());
  REQUIRE(cmd->steps->value == 500);
}

TEST_CASE("parseCommand: setSpeed", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"setSpeed","speed":1500})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::SetSpeed);
  REQUIRE(cmd->speed.has_value());
  REQUIRE(*cmd->speed == 1500);
}

TEST_CASE("parseCommand: setAccel", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"setAccel","accel":200})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::SetAccel);
  REQUIRE(cmd->accel.has_value());
  REQUIRE(*cmd->accel == 200);
}

TEST_CASE("parseCommand: setVolume", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"setVolume","targetVolume":25.0})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::SetVolume);
  REQUIRE(cmd->targetVolume.has_value());
  REQUIRE(approx(cmd->targetVolume->value, 25.0f));
}

TEST_CASE("parseCommand: valve.setPosition input", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"valve.setPosition","position":"input"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::ValveSetPosition);
  REQUIRE(cmd->valvePos.has_value());
  REQUIRE(*cmd->valvePos == ValvePosition::Input);
}

TEST_CASE("parseCommand: valve.setPosition output", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"valve.setPosition","position":"output"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::ValveSetPosition);
  REQUIRE(cmd->valvePos.has_value());
  REQUIRE(*cmd->valvePos == ValvePosition::Output);
}

TEST_CASE("parseCommand: valve.getState", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"valve.getState"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::ValveGetState);
}

TEST_CASE("parseCommand: temperature.read", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"temperature.read"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::TempRead);
}

TEST_CASE("parseCommand: adc.cal.get", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"adc.cal.get"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::AdcCalGet);
}

TEST_CASE("parseCommand: adc.cal.save", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"adc.cal.save","aX1000":1200,"b":5})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::AdcCalSave);
}

TEST_CASE("parseCommand: stallGuard.get", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"stallGuard.get"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::StallGuardGet);
}

TEST_CASE("parseCommand: stallGuard.setThreshold", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"stallGuard.setThreshold","threshold":64})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::StallGuardSetThreshold);
  REQUIRE(cmd->sgThreshold.has_value());
  REQUIRE(*cmd->sgThreshold == 64);
}

TEST_CASE("parseCommand: cal.get", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"cal.get"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::CalGet);
}

TEST_CASE("parseCommand: cal.save", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"cal.save","stepsPerMl":1500.0,"nominalVolume":25.0})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::CalSave);
}

TEST_CASE("parseCommand: system.reboot", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"system.reboot"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::SystemReboot);
}

TEST_CASE("parseCommand: system.firmwareVersion", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"system.firmwareVersion"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::SystemFirmwareVersion);
}

TEST_CASE("parseCommand: system.getStatus", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"system.getStatus"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::SystemGetStatus);
}

TEST_CASE("parseCommand: configMove", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"configMove","moveSpeed":2000,"moveAccel":300})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::ConfigMove);
  REQUIRE(cmd->configMoveSpeed.has_value());
  REQUIRE(*cmd->configMoveSpeed == 2000);
  REQUIRE(cmd->configMoveAccel.has_value());
  REQUIRE(*cmd->configMoveAccel == 300);
}

TEST_CASE("parseCommand: configHome", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"configHome","homeSpeed":500})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::ConfigHome);
  REQUIRE(cmd->configHomeSpeed.has_value());
  REQUIRE(*cmd->configHomeSpeed == 500);
}

TEST_CASE("parseCommand: configSensor", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"configSensor","sensorValue":42})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::ConfigSensor);
  REQUIRE(cmd->configSensorValue.has_value());
  REQUIRE(*cmd->configSensorValue == 42);
}

TEST_CASE("parseCommand: cal.run", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"cal.run"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::CalRun);
}

TEST_CASE("parseCommand: cal.getResult", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"cal.getResult"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::CalGetResult);
}

TEST_CASE("parseCommand: rinse", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"rinse"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::Rinse);
}

TEST_CASE("parseCommand: stop", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"stop"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::Stop);
}

TEST_CASE("parseCommand: emergencyStop", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"emergencyStop"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::EmergencyStop);
}

TEST_CASE("parseCommand: empty", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"empty"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::Empty);
}

TEST_CASE("parseCommand: getStatus", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"getStatus"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::GetStatus);
}

TEST_CASE("parseCommand: invalid JSON", "[command]") {
  auto cmd = parseCommand("not json");
  REQUIRE(!cmd);
  REQUIRE(cmd.error() == ProtocolError::InvalidJson);
}

TEST_CASE("parseCommand: missing cmd field", "[command]") {
  auto cmd = parseCommand(R"({"foo":"bar"})");
  REQUIRE(!cmd);
  REQUIRE(cmd.error() == ProtocolError::MissingParam);
}

TEST_CASE("parseCommand: unknown command", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"unknown.command"})");
  REQUIRE(!cmd);
  REQUIRE(cmd.error() == ProtocolError::UnknownCommand);
}

TEST_CASE("makeAckThenResponse", "[command]") {
  auto rsp = makeAckThenResponse();
  REQUIRE(rsp.kind == ResponseKind::AckThen);
  REQUIRE(rsp.bodySize > 0);
  std::string_view sv(rsp.body.data(), rsp.bodySize);
  REQUIRE(sv.find("\"ack\"") != std::string_view::npos);
}

TEST_CASE("makeErrorResponse", "[command]") {
  auto rsp = makeErrorResponse("test error");
  REQUIRE(rsp.kind == ResponseKind::Error);
  std::string_view sv(rsp.body.data(), rsp.bodySize);
  REQUIRE(sv.find("test error") != std::string_view::npos);
}

TEST_CASE("serializeToBuffer: AckThen fills buffer", "[command]") {
  auto rsp = makeAckThenResponse();
  memory::ResponseBuffer buf{};
  auto result = serializeToBuffer(rsp, buf);
  REQUIRE(result);
  REQUIRE(*result > 0);
  std::string_view sv(buf.data(), *result);
  REQUIRE(sv.find("ack") != std::string_view::npos);
}

TEST_CASE("serializeToBuffer: NoResponse produces empty", "[command]") {
  CommandResponse rsp;
  rsp.kind = ResponseKind::NoResponse;
  memory::ResponseBuffer buf{};
  auto result = serializeToBuffer(rsp, buf);
  REQUIRE(result);
  REQUIRE(*result == 0);
}
