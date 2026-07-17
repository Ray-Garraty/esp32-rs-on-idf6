#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>
#include <type_traits>

#include "application/dispatch.hpp"
#include "application/motor_controller.hpp"
#include "domain/sm_result.hpp"

using namespace ecotiter::application;
using namespace ecotiter::domain;

// ============================================================================
// Mock implementation for testing the interface contract
// ============================================================================

class MockMotorController : public IMotorController
{
public:
    bool sendCommandResult{true};
    std::optional<SmResult> peekResultValue{std::nullopt};
    std::optional<SmResult> waitResultValue{std::nullopt};
    uint32_t waitResultTimeoutMs{0};

    bool sendCommand(const char* cmdJson, size_t len) override
    {
        (void)len;
        lastCmdJson = std::string(cmdJson);
        return sendCommandResult;
    }

    bool readTmcRegister(uint8_t reg, uint32_t& value) override
    {
        (void)reg;
        (void)value;
        return true;
    }

    std::optional<SmResult> peekResult() override { return peekResultValue; }

    std::optional<SmResult> waitResult(uint32_t timeoutMs) override
    {
        waitResultTimeoutMs = timeoutMs;
        return waitResultValue;
    }

    // Test helpers
    std::string lastCmdJson;
};

// ============================================================================
// Tests
// ============================================================================

TEST_CASE("IMotorController: setMotorController stores and getMotorController "
          "retrieves the instance",
          "[motor_controller]")
{
    // Initially no controller
    REQUIRE(getMotorController() == nullptr);

    {
        MockMotorController mock;
        setMotorController(&mock);
        REQUIRE(getMotorController() == &mock);

        // Verify we can call methods on the retrieved pointer
        auto* ctrl = getMotorController();
        REQUIRE(ctrl != nullptr);

        bool sent = ctrl->sendCommand(R"({"cmd":"stop"})", 15);
        REQUIRE(sent);
        REQUIRE(mock.lastCmdJson == R"({"cmd":"stop"})");

        auto result = ctrl->peekResult();
        REQUIRE_FALSE(result.has_value());

        // Reset for next test
        setMotorController(nullptr);
    }

    // After mock destroyed, controller is null
    REQUIRE(getMotorController() == nullptr);
}

TEST_CASE("IMotorController: sendCommand returns true/false correctly", "[motor_controller]")
{
    MockMotorController mock;

    mock.sendCommandResult = true;
    bool sent = mock.sendCommand(R"({"cmd":"stop"})", 15);
    REQUIRE(sent);

    mock.sendCommandResult = false;
    sent = mock.sendCommand(R"({"cmd":"stop"})", 15);
    REQUIRE_FALSE(sent);
}

TEST_CASE("IMotorController: peekResult returns expected value", "[motor_controller]")
{
    MockMotorController mock;

    // No result
    mock.peekResultValue = std::nullopt;
    auto result = mock.peekResult();
    REQUIRE_FALSE(result.has_value());

    // With result
    SmResult sr;
    sr.type = SmResult::Type::RinseComplete;
    sr.stepsTaken = 100;
    sr.measuredSpeedMlMin = 20.0f;
    sr.results[0] = 0.0f;
    sr.resultCount = 0;
    mock.peekResultValue = sr;

    result = mock.peekResult();
    REQUIRE(result.has_value());
    REQUIRE(result->type == SmResult::Type::RinseComplete);
    REQUIRE(result->stepsTaken == 100);
}

TEST_CASE("IMotorController: waitResult returns expected value with timeout", "[motor_controller]")
{
    MockMotorController mock;

    // Timeout (no result)
    mock.waitResultValue = std::nullopt;
    auto result = mock.waitResult(5000);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(mock.waitResultTimeoutMs == 5000);

    // With result
    SmResult sr;
    sr.type = SmResult::Type::CalDoseComplete;
    sr.stepsTaken = 500;
    sr.measuredSpeedMlMin = 0.0f;
    sr.results[0] = 0.0f;
    sr.resultCount = 0;
    mock.waitResultValue = sr;

    result = mock.waitResult(10000);
    REQUIRE(result.has_value());
    REQUIRE(result->type == SmResult::Type::CalDoseComplete);
    REQUIRE(result->stepsTaken == 500);
    REQUIRE(mock.waitResultTimeoutMs == 10000);
}

// Compile-time check: IMotorController has pure virtual methods and cannot
// be instantiated directly. The following would fail to compile:
//   IMotorController c;  // error: cannot declare variable 'c' to be of abstract type
static_assert(!std::is_constructible_v<IMotorController>, "IMotorController must be abstract");
