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
  auto cmd = parseCommand(R"({"cmd":"burette.fill"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::Fill);
}

TEST_CASE("parseCommand: doseVolume with volume", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"burette.doseVolume","volume":10.5})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::DoseVolume);
  REQUIRE(cmd->volume.has_value());
  REQUIRE(approx(cmd->volume->value, 10.5f));
}

TEST_CASE("parseCommand: setDirection liq_in", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"burette.setDirection","direction":"liq_in"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::SetDirection);
  REQUIRE(cmd->direction.has_value());
  REQUIRE(*cmd->direction == Direction::LiqIn);
}

TEST_CASE("parseCommand: setDirection liq_out", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"burette.setDirection","direction":"liq_out"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::SetDirection);
  REQUIRE(cmd->direction.has_value());
  REQUIRE(*cmd->direction == Direction::LiqOut);
}

TEST_CASE("parseCommand: moveSteps", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"burette.moveSteps","steps":500})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::MoveSteps);
  REQUIRE(cmd->steps.has_value());
  REQUIRE(cmd->steps->value == 500);
}

TEST_CASE("parseCommand: setSpeed", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"burette.setSpeed","speed":1500})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::SetSpeed);
  REQUIRE(cmd->speed.has_value());
  REQUIRE(*cmd->speed == 1500);
}

TEST_CASE("parseCommand: setAccel", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"burette.setAccel","accel":200})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::SetAccel);
  REQUIRE(cmd->accel.has_value());
  REQUIRE(*cmd->accel == 200);
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

TEST_CASE("parseCommand: adc.cal.measure", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"adc.cal.measure"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::AdcCalMeasure);
}

TEST_CASE("parseCommand: adc.cal.measure with ref_mv", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"adc.cal.measure","ref_mv":1500})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::AdcCalMeasure);
  REQUIRE(cmd->refMv.has_value());
  REQUIRE(*cmd->refMv == 1500.0f);
}

TEST_CASE("parseCommand: adc.cal.compute", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"adc.cal.compute"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::AdcCalCompute);
}

TEST_CASE("parseCommand: adc.cal.reset", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"adc.cal.reset"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::AdcCalReset);
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
  auto cmd = parseCommand(R"({"cmd":"burette.cal.get"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::CalGet);
}

TEST_CASE("parseCommand: cal.save", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"burette.cal.save","stepsPerMl":1500.0,"nominalVolume":25.0})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::CalSave);
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

TEST_CASE("parseCommand: cal.run", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"burette.cal.run"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::CalRun);
}

TEST_CASE("parseCommand: cal.getResult", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"burette.cal.getResult"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::CalGetResult);
}

TEST_CASE("parseCommand: rinse", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"burette.rinse"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::Rinse);
}

TEST_CASE("parseCommand: stop", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"burette.stop"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::Stop);
}

TEST_CASE("parseCommand: emergencyStop", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"burette.emergencyStop"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::EmergencyStop);
}

TEST_CASE("parseCommand: empty", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"burette.empty"})");
  REQUIRE(cmd);
  REQUIRE(cmd->type == CommandType::Empty);
}

TEST_CASE("parseCommand: getStatus", "[command]") {
  auto cmd = parseCommand(R"({"cmd":"burette.getStatus"})");
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
  REQUIRE(sv.find("\"status\":\"ok\"") != std::string_view::npos);
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
  REQUIRE(sv.find("\"status\":\"ok\"") != std::string_view::npos);
}

TEST_CASE("serializeToBuffer: NoResponse produces empty", "[command]") {
  CommandResponse rsp;
  rsp.kind = ResponseKind::NoResponse;
  memory::ResponseBuffer buf{};
  auto result = serializeToBuffer(rsp, buf);
  REQUIRE(result);
  REQUIRE(*result == 0);
}
