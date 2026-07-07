#include <catch2/catch_test_macros.hpp>
#include <domain/burette.hpp>

using namespace ecotiter::domain;

TEST_CASE("Burette starts in Idle", "[burette]") {
    BuretteController ctrl;
    REQUIRE(ctrl.state == BuretteState::Idle);
}

TEST_CASE("Fill from Idle succeeds", "[burette]") {
    BuretteController ctrl;
    auto result = ctrl.transition(BuretteCommand::Fill);
    REQUIRE(result);
    REQUIRE(ctrl.state == BuretteState::Filling);
}

TEST_CASE("Dose from Idle succeeds", "[burette]") {
    BuretteController ctrl;
    auto result = ctrl.transition(BuretteCommand::Dose);
    REQUIRE(result);
    REQUIRE(ctrl.state == BuretteState::Dosing);
}

TEST_CASE("Stop during active state succeeds", "[burette]") {
    BuretteController ctrl;
    std::ignore = ctrl.transition(BuretteCommand::Fill);
    auto result = ctrl.transition(BuretteCommand::Stop);
    REQUIRE(result);
    REQUIRE(ctrl.state == BuretteState::Stopping);
}

TEST_CASE("Stop from Idle fails", "[burette]") {
    BuretteController ctrl;
    auto result = ctrl.transition(BuretteCommand::Stop);
    REQUIRE(!result);
    REQUIRE(result.error() == StateError::InvalidTransition);
}

TEST_CASE("Fill when already busy fails", "[burette]") {
    BuretteController ctrl;
    std::ignore = ctrl.transition(BuretteCommand::Fill);
    auto result = ctrl.transition(BuretteCommand::Dose);
    REQUIRE(!result);
    REQUIRE(result.error() == StateError::Busy);
}

TEST_CASE("Reset only from Error", "[burette]") {
    BuretteController ctrl;
    auto result = ctrl.transition(BuretteCommand::Reset);
    REQUIRE(!result);
    REQUIRE(result.error() == StateError::InvalidTransition);
}
