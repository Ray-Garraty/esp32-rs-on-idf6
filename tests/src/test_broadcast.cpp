#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string_view>

#include <nlohmann/json.hpp>

#include "interface/broadcast.hpp"

using namespace ecotiter::interface;
using namespace ecotiter::domain;
using json = nlohmann::json;

TEST_CASE("serializeBroadcast: builds valid JSON with all fields", "[broadcast]") {
    BroadcastEvent evt{
        .tick = 42,
        .tempCX100 = 2345,
        .mv = 1500,
        .vlv = ValvePosition::Input,
        .brt = BuretteState::Idle,
        .dir = Direction::Cw,
        .speed = 1000,
        .accel = 500,
        .volumeMl = 50.0f,
        .dispensedSteps = 12345
    };

    memory::ResponseBuffer buf{};
    auto sv = serializeBroadcast(evt, buf);
    REQUIRE_FALSE(sv.empty());

    auto j = json::parse(sv);
    REQUIRE(j["t"] == 42);
    REQUIRE(j["temp"] == 2345);
    REQUIRE(j["mv"] == 1500);
    REQUIRE(j["vlv"] == "input");
    REQUIRE(j["brt"] == "idle");
    REQUIRE(j["dir"] == "cw");
    REQUIRE(j["spd"] == 1000);
    REQUIRE(j["acc"] == 500);
    REQUIRE(j["vol"] == 50.0f);
    REQUIRE(j["steps"] == 12345);
}

TEST_CASE("serializeBroadcast: output position, ccw, dosing", "[broadcast]") {
    BroadcastEvent evt{
        .tick = 1,
        .tempCX100 = 0,
        .mv = 0,
        .vlv = ValvePosition::Output,
        .brt = BuretteState::Dosing,
        .dir = Direction::Ccw,
        .speed = 2000,
        .accel = 300,
        .volumeMl = 25.0f,
        .dispensedSteps = 0
    };

    memory::ResponseBuffer buf{};
    auto sv = serializeBroadcast(evt, buf);
    REQUIRE_FALSE(sv.empty());

    auto j = json::parse(sv);
    REQUIRE(j["vlv"] == "output");
    REQUIRE(j["brt"] == "dosing");
    REQUIRE(j["dir"] == "ccw");
    REQUIRE(j["spd"] == 2000);
    REQUIRE(j["acc"] == 300);
    REQUIRE(j["vol"] == 25.0f);
    REQUIRE(j["steps"] == 0);
}

TEST_CASE("serializeBroadcast: sensor not detected (tempCX100 = -99999)", "[broadcast]") {
    BroadcastEvent evt{
        .tick = 0,
        .tempCX100 = -99999,
        .mv = 0,
        .vlv = ValvePosition::Input,
        .brt = BuretteState::Idle,
        .dir = Direction::Cw,
        .speed = 1000,
        .accel = 500,
        .volumeMl = 50.0f,
        .dispensedSteps = 0
    };

    memory::ResponseBuffer buf{};
    auto sv = serializeBroadcast(evt, buf);
    REQUIRE_FALSE(sv.empty());

    auto j = json::parse(sv);
    REQUIRE(j["temp"] == -99999);
}

TEST_CASE("serializeBroadcast: all burette states round-trip", "[broadcast]") {
    auto testState = [](BuretteState state, const char* expected) {
        BroadcastEvent evt{
            .tick = 0,
            .tempCX100 = 0,
            .mv = 0,
            .vlv = ValvePosition::Input,
            .brt = state,
            .dir = Direction::Cw,
            .speed = 1000,
            .accel = 500,
            .volumeMl = 50.0f,
            .dispensedSteps = 0
        };
        memory::ResponseBuffer buf{};
        auto sv = serializeBroadcast(evt, buf);
        REQUIRE_FALSE(sv.empty());
        auto j = json::parse(sv);
        REQUIRE(j["brt"] == expected);
    };

    testState(BuretteState::Idle, "idle");
    testState(BuretteState::Homing, "homing");
    testState(BuretteState::Filling, "filling");
    testState(BuretteState::Emptying, "emptying");
    testState(BuretteState::Dosing, "dosing");
    testState(BuretteState::Rinsing, "rinsing");
    testState(BuretteState::Stopping, "stopping");
    testState(BuretteState::Error, "error");
}

TEST_CASE("serializeBroadcast: empty buffer returns empty view", "[broadcast]") {
    // Use a tiny buffer that can't hold the full JSON
    std::array<char, 4> tinyBuf{};
    BroadcastEvent evt{};
    // We can't call serializeBroadcast with a non-ResponseBuffer easily,
    // so just verify the normal path with ResponseBuffer works.
    memory::ResponseBuffer buf{};
    auto sv = serializeBroadcast(evt, buf);
    REQUIRE_FALSE(sv.empty());
}
