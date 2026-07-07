#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <atomic>

static std::atomic<uint8_t> gValvePosition{0};

TEST_CASE("Default valve position is Input", "[valve]") {
    REQUIRE(gValvePosition.load() == 0);
}

TEST_CASE("Set and get valve position", "[valve]") {
    gValvePosition.store(1);
    REQUIRE(gValvePosition.load() == 1);

    gValvePosition.store(0);
    REQUIRE(gValvePosition.load() == 0);
}
