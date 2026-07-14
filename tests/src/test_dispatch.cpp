#include <catch2/catch_test_macros.hpp>

#include "application/command.hpp"
#include "application/dispatch.hpp"
#include "domain/errors.hpp"

using namespace ecotiter::application;
using namespace ecotiter::domain;

TEST_CASE("dispatch: serial.ping returns ok", "[dispatch]") {
  Command cmd{CommandType::SerialPing};
  auto rsp = dispatch(cmd);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
  std::string_view sv(rsp->body.data(), rsp->bodySize);
  REQUIRE(sv.find("\"status\":\"ok\"") != std::string_view::npos);
}

TEST_CASE("dispatch: fill returns AckThen", "[dispatch]") {
  Command cmd{CommandType::Fill};
  auto rsp = dispatch(cmd);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::AckThen);
}

TEST_CASE("dispatch: empty returns AckThen", "[dispatch]") {
  Command cmd{CommandType::Empty};
  auto rsp = dispatch(cmd);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::AckThen);
}

TEST_CASE("dispatch: doseVolume missing param returns error", "[dispatch]") {
  Command cmd{CommandType::DoseVolume};
  auto rsp = dispatch(cmd);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Error);
}

TEST_CASE("dispatch: doseVolume with param returns AckThen", "[dispatch]") {
  Command cmd{CommandType::DoseVolume};
  cmd.volume = Ml{10.0f};
  auto rsp = dispatch(cmd);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::AckThen);
}

TEST_CASE("dispatch: rinse with cycles returns AckThen", "[dispatch]") {
  Command cmd{CommandType::Rinse};
  cmd.volume = Ml{3.0f};
  auto rsp = dispatch(cmd);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::AckThen);
}

TEST_CASE("dispatch: stop returns Single", "[dispatch]") {
  Command cmd{CommandType::Stop};
  auto rsp = dispatch(cmd);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
}

TEST_CASE("dispatch: emergencyStop returns Single", "[dispatch]") {
  Command cmd{CommandType::EmergencyStop};
  auto rsp = dispatch(cmd);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
}

TEST_CASE("dispatch: getStatus returns JSON", "[dispatch]") {
  Command cmd{CommandType::GetStatus};
  auto rsp = dispatch(cmd);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
  REQUIRE(rsp->bodySize > 0);
  std::string_view sv(rsp->body.data(), rsp->bodySize);
  REQUIRE(sv.find("\"status\":\"ok\"") != std::string_view::npos);
}

TEST_CASE("dispatch: setDirection returns error without param", "[dispatch]") {
  Command cmd{CommandType::SetDirection};
  auto rsp = dispatch(cmd);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Error);
}

TEST_CASE("dispatch: setDirection with param returns AckThen", "[dispatch]") {
  Command cmd{CommandType::SetDirection};
  cmd.direction = Direction::LiqIn;
  auto rsp = dispatch(cmd);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::AckThen);
}

TEST_CASE("dispatch: setSpeed returns error without param", "[dispatch]") {
  Command cmd{CommandType::SetSpeed};
  auto rsp = dispatch(cmd);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Error);
}

TEST_CASE("dispatch: setSpeed with param returns AckThen", "[dispatch]") {
  Command cmd{CommandType::SetSpeed};
  cmd.speed = 1500;
  auto rsp = dispatch(cmd);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::AckThen);
}

TEST_CASE("dispatch: moveSteps returns error without param", "[dispatch]") {
  Command cmd{CommandType::MoveSteps};
  auto rsp = dispatch(cmd);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Error);
}

TEST_CASE("dispatch: moveSteps with param returns AckThen", "[dispatch]") {
  Command cmd{CommandType::MoveSteps};
  cmd.steps = Steps{200};
  auto rsp = dispatch(cmd);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::AckThen);
}

TEST_CASE("dispatch: valve.setPosition returns error without param", "[dispatch]") {
  Command cmd{CommandType::ValveSetPosition};
  auto rsp = dispatch(cmd);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Error);
}

TEST_CASE("dispatch: valve.setPosition with param returns success", "[dispatch]") {
  Command cmd{CommandType::ValveSetPosition};
  cmd.valvePos = ValvePosition::Output;
  auto rsp = dispatch(cmd);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
}

TEST_CASE("dispatch: valve.getState returns JSON", "[dispatch]") {
  Command cmd{CommandType::ValveGetState};
  auto rsp = dispatch(cmd);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
}

TEST_CASE("dispatch: temperature.read returns JSON", "[dispatch]") {
  Command cmd{CommandType::TempRead};
  auto rsp = dispatch(cmd);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
}

TEST_CASE("dispatch: adc.cal.get returns JSON", "[dispatch]") {
  Command cmd{CommandType::AdcCalGet};
  auto rsp = dispatch(cmd);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
}

TEST_CASE("dispatch: stallGuard.get returns JSON", "[dispatch]") {
  Command cmd{CommandType::StallGuardGet};
  auto rsp = dispatch(cmd);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
}

TEST_CASE("dispatch: adc.cal.measure returns error without ref_mv", "[dispatch]") {
  Command cmd{CommandType::AdcCalMeasure};
  auto rsp = dispatch(cmd);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Error);
}

TEST_CASE("dispatch: adc.cal.reset returns JSON", "[dispatch]") {
  Command cmd{CommandType::AdcCalReset};
  auto rsp = dispatch(cmd);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
}

TEST_CASE("dispatch: system.firmwareVersion returns JSON", "[dispatch]") {
  Command cmd{CommandType::SystemFirmwareVersion};
  auto rsp = dispatch(cmd);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
}

TEST_CASE("dispatch: system.getStatus returns JSON", "[dispatch]") {
  Command cmd{CommandType::SystemGetStatus};
  auto rsp = dispatch(cmd);
  REQUIRE(rsp);
  REQUIRE(rsp->kind == ResponseKind::Single);
}
