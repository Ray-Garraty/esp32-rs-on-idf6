#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string_view>

#include <nlohmann/json.hpp>

// Include the core logic header directly (no ESP-IDF dependency)
#include "interface/rest_api.hpp"
#include "domain/types.hpp"

// The *_Core functions are defined in rest_api.cpp which depends on ESP-IDF.
// For host tests we need to provide our own implementations of the core functions
// or link against a host-compilable subset.
//
// Strategy: we include the header but provide test-only implementations
// of the core functions that test the command parsing/dispatch logic.

using namespace ecotiter::interface;
using namespace ecotiter::domain;
using namespace ecotiter::domain::memory;
using json = nlohmann::json;

// We test the core command processing logic inline since the actual
// rest_api.cpp depends on ESP-IDF's httpd and application dispatch.
// These tests verify the protocol handling independently.

TEST_CASE("ping handler logic: returns status ok", "[rest_api]") {
    memory::ResponseBuffer buf{};
    auto result = handlePingCore(buf);
    REQUIRE(result);
    REQUIRE(*result > 0);

    std::string_view sv(buf.data(), *result);
    auto j = json::parse(sv);
    REQUIRE(j["status"] == "ok");
}

// Note: handleCommandCore, handleStatusCore, handleValveGetCore, handleValvePostCore
// are fully implemented in rest_api.cpp and link against application::dispatch(),
// application::parseCommand(), and the domain globals.
//
// For comprehensive testing of command parsing and dispatch, see:
//   test_command.cpp — parseCommand() unit tests (133+ cases)
//   test_dispatch.cpp — dispatch routing tests
//   test_handlers.cpp — handler logic tests
//
// The valve*Core functions are tested in test_valve.cpp for command parsing.

TEST_CASE("handleCommandCore: valid ping command returns response", "[rest_api]") {
    // This test can run because rest_api.cpp links against application library
    // which provides parseCommand and dispatch.
    memory::ResponseBuffer buf{};
    auto result = handleCommandCore(R"({"cmd":"serial.ping"})", buf);
    REQUIRE(result);
    REQUIRE(*result > 0);

    std::string_view sv(buf.data(), *result);
    auto j = json::parse(sv);
    // The ping response should contain "result": "pong"
    // or some indication of success
    REQUIRE(sv.find("pong") != std::string_view::npos);
}

TEST_CASE("handleCommandCore: invalid JSON returns 400", "[rest_api]") {
    memory::ResponseBuffer buf{};
    auto result = handleCommandCore("not json", buf);
    REQUIRE_FALSE(result);
    REQUIRE(result.error() == 400);
}

TEST_CASE("handleCommandCore: missing cmd field returns 400", "[rest_api]") {
    memory::ResponseBuffer buf{};
    auto result = handleCommandCore(R"({"foo":"bar"})", buf);
    REQUIRE_FALSE(result);
    REQUIRE(result.error() == 400);
}

TEST_CASE("handleCommandCore: unknown command returns 400", "[rest_api]") {
    memory::ResponseBuffer buf{};
    auto result = handleCommandCore(R"({"cmd":"unknown.command"})", buf);
    REQUIRE_FALSE(result);
    REQUIRE(result.error() == 400);
}

TEST_CASE("handleValveGetCore: returns current valve state", "[rest_api]") {
    // Set known state
    gValvePosition.store(ValvePosition::Input, std::memory_order_release);

    memory::ResponseBuffer buf{};
    auto result = handleValveGetCore(buf);
    REQUIRE(result);
    REQUIRE(*result > 0);

    std::string_view sv(buf.data(), *result);
    auto j = json::parse(sv);
    REQUIRE(j["valve"] == "input");

    // Change state and verify
    gValvePosition.store(ValvePosition::Output, std::memory_order_release);

    result = handleValveGetCore(buf);
    REQUIRE(result);
    sv = std::string_view(buf.data(), *result);
    j = json::parse(sv);
    REQUIRE(j["valve"] == "output");
}

TEST_CASE("handleValvePostCore: set valve to input", "[rest_api]") {
    gValvePosition.store(ValvePosition::Output, std::memory_order_release); // start at output

    memory::ResponseBuffer buf{};
    auto result = handleValvePostCore(R"({"position":"input"})", buf);
    REQUIRE(result);
    REQUIRE(*result > 0);

    std::string_view sv(buf.data(), *result);
    auto j = json::parse(sv);
    REQUIRE(j["valve"] == "input");

    // Verify global was updated
    REQUIRE(gValvePosition.load(std::memory_order_acquire) == ValvePosition::Input);
}

TEST_CASE("handleValvePostCore: invalid position returns 400", "[rest_api]") {
    memory::ResponseBuffer buf{};
    auto result = handleValvePostCore(R"({"position":"invalid"})", buf);
    REQUIRE_FALSE(result);
    REQUIRE(result.error() == 400);
}

TEST_CASE("handleValvePostCore: missing position returns 400", "[rest_api]") {
    memory::ResponseBuffer buf{};
    auto result = handleValvePostCore(R"({"foo":"bar"})", buf);
    REQUIRE_FALSE(result);
    REQUIRE(result.error() == 400);
}

TEST_CASE("handleValvePostCore: invalid JSON returns 400", "[rest_api]") {
    memory::ResponseBuffer buf{};
    auto result = handleValvePostCore("not json", buf);
    REQUIRE_FALSE(result);
    REQUIRE(result.error() == 400);
}
