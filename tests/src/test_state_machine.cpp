#include <catch2/catch_test_macros.hpp>

#include "application/state_machine.hpp"

using namespace ecotiter::application;
using namespace ecotiter::domain;

TEST_CASE("ASM: starts in Idle + UsbActive", "[state_machine]") {
  ApplicationStateMachine asm_;
  REQUIRE(asm_.buretteState() == BuretteState::Idle);
  REQUIRE(asm_.transportState() == TransportState::UsbActive);
  REQUIRE(asm_.isReady());
  REQUIRE_FALSE(asm_.isEmergency());
}

TEST_CASE("ASM: setTransportState", "[state_machine]") {
  ApplicationStateMachine asm_;
  asm_.setTransportState(TransportState::BleConnected);
  REQUIRE(asm_.transportState() == TransportState::BleConnected);
}

TEST_CASE("ASM: apply Fill transitions to Filling", "[state_machine]") {
  ApplicationStateMachine asm_;
  auto result = asm_.apply(BuretteCommand::Fill);
  REQUIRE(result);
  REQUIRE(asm_.buretteState() == BuretteState::Filling);
  REQUIRE_FALSE(asm_.isReady());
}

TEST_CASE("ASM: apply Dose transitions to Dosing", "[state_machine]") {
  ApplicationStateMachine asm_;
  auto result = asm_.apply(BuretteCommand::Dose);
  REQUIRE(result);
  REQUIRE(asm_.buretteState() == BuretteState::Dosing);
}

TEST_CASE("ASM: apply Empty transitions to Emptying", "[state_machine]") {
  ApplicationStateMachine asm_;
  auto result = asm_.apply(BuretteCommand::Empty);
  REQUIRE(result);
  REQUIRE(asm_.buretteState() == BuretteState::Emptying);
}

TEST_CASE("ASM: apply Rinse transitions to Rinsing", "[state_machine]") {
  ApplicationStateMachine asm_;
  auto result = asm_.apply(BuretteCommand::Rinse);
  REQUIRE(result);
  REQUIRE(asm_.buretteState() == BuretteState::Rinsing);
}

TEST_CASE("ASM: apply Stop during active state succeeds", "[state_machine]") {
  ApplicationStateMachine asm_;
  std::ignore = asm_.apply(BuretteCommand::Fill);
  auto result = asm_.apply(BuretteCommand::Stop);
  REQUIRE(result);
  REQUIRE(asm_.buretteState() == BuretteState::Stopping);
}

TEST_CASE("ASM: apply EmergencyStop during active state", "[state_machine]") {
  ApplicationStateMachine asm_;
  std::ignore = asm_.apply(BuretteCommand::Fill);
  auto result = asm_.apply(BuretteCommand::EmergencyStop);
  REQUIRE(result);
  REQUIRE(asm_.buretteState() == BuretteState::Stopping);
  REQUIRE(asm_.isEmergency());
}

TEST_CASE("ASM: apply Fill when busy fails", "[state_machine]") {
  ApplicationStateMachine asm_;
  std::ignore = asm_.apply(BuretteCommand::Fill);
  auto result = asm_.apply(BuretteCommand::Dose);
  REQUIRE_FALSE(result);
  REQUIRE(result.error() == StateError::Busy);
}

TEST_CASE("ASM: startOperation and tick timeout", "[state_machine]") {
  ApplicationStateMachine asm_;
  std::ignore = asm_.apply(BuretteCommand::Fill);
  REQUIRE(asm_.buretteState() == BuretteState::Filling);

  asm_.startOperation(0, 5); // duration 5 ticks
  REQUIRE_FALSE(asm_.isReady());

  // Tick at tick 3 — still running
  bool completed = asm_.tick(3);
  REQUIRE_FALSE(completed);
  REQUIRE(asm_.buretteState() == BuretteState::Filling);

  // Tick at tick 5 — timeout, reset to Idle
  completed = asm_.tick(5);
  REQUIRE(completed);
  REQUIRE(asm_.buretteState() == BuretteState::Idle);
  REQUIRE(asm_.isReady());
}

TEST_CASE("ASM: tick without pending operation does nothing", "[state_machine]") {
  ApplicationStateMachine asm_;
  bool completed = asm_.tick(100);
  REQUIRE_FALSE(completed);
  REQUIRE(asm_.buretteState() == BuretteState::Idle);
}

TEST_CASE("ASM: reset clears pending and sets Idle", "[state_machine]") {
  ApplicationStateMachine asm_;
  std::ignore = asm_.apply(BuretteCommand::Fill);
  asm_.startOperation(0, 100);
  REQUIRE_FALSE(asm_.isReady());

  asm_.reset();
  REQUIRE(asm_.buretteState() == BuretteState::Idle);
  REQUIRE(asm_.isReady());
}

TEST_CASE("ASM: EmergencyStop → isEmergency true", "[state_machine]") {
  ApplicationStateMachine asm_;
  std::ignore = asm_.apply(BuretteCommand::Fill);
  std::ignore = asm_.apply(BuretteCommand::EmergencyStop);
  REQUIRE(asm_.isEmergency());
}
