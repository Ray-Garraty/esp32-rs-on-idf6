#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

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
    REQUIRE(j["temp"] == Catch::Approx(23.4));
    REQUIRE(j["mv"] == 1500);
    REQUIRE(j["vlv"] == "in");
    REQUIRE(j["brt"]["sts"] == "idle");
    REQUIRE(j["brt"]["vl"] == Catch::Approx(50.0));
    REQUIRE(j["brt"]["spd"] == Catch::Approx(7.8));
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
    REQUIRE(j["vlv"] == "out");
    REQUIRE(j["brt"]["sts"] == "working");
    REQUIRE(j["brt"]["vl"] == Catch::Approx(25.0));
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
    REQUIRE(j["temp"].is_null());
}

TEST_CASE("serializeBroadcast: all burette states round-trip", "[broadcast]") {
    auto testState = [](BuretteState state, const char* expectedSts, bool expectVlNull) {
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
        REQUIRE(j["brt"]["sts"] == expectedSts);
        if (expectVlNull) {
            REQUIRE(j["brt"]["vl"].is_null());
        } else {
            REQUIRE(j["brt"]["vl"] == Catch::Approx(50.0));
        }
    };

    testState(BuretteState::Idle, "idle", false);
    testState(BuretteState::Homing, "working", true);
    testState(BuretteState::Filling, "working", false);
    testState(BuretteState::Emptying, "working", false);
    testState(BuretteState::Dosing, "working", false);
    testState(BuretteState::Rinsing, "working", false);
    testState(BuretteState::Stopping, "working", false);
    testState(BuretteState::Error, "error", false);
}

TEST_CASE("serializeBroadcast: empty buffer returns empty view", "[broadcast]") {
    memory::ResponseBuffer buf{};
    BroadcastEvent evt{};
    auto sv = serializeBroadcast(evt, buf);
    REQUIRE_FALSE(sv.empty());
}

TEST_CASE("serializeBroadcast: reads from domain atoms produce valid JSON", "[broadcast]") {
    gTempCX100.store(2500, std::memory_order_release);
    gLastMv.store(1800, std::memory_order_release);

    BroadcastEvent evt{
        .tick = 1,
        .tempCX100 = gTempCX100.load(std::memory_order_acquire),
        .mv = gLastMv.load(std::memory_order_acquire),
        .vlv = gValvePosition.load(std::memory_order_acquire),
        .brt = gBuretteState.load(std::memory_order_acquire),
        .dir = gDirection.load(std::memory_order_acquire),
        .speed = gSpeed.load(std::memory_order_acquire),
        .accel = gAccel.load(std::memory_order_acquire),
        .volumeMl = gVolumeMl.load(std::memory_order_acquire),
        .dispensedSteps = gDispensedSteps.load(std::memory_order_acquire)
    };

    memory::ResponseBuffer buf{};
    auto sv = serializeBroadcast(evt, buf);
    REQUIRE_FALSE(sv.empty());

    auto j = json::parse(sv);
    REQUIRE(j["temp"] == Catch::Approx(25.0));
    REQUIRE(j["mv"] == 1800);
}
