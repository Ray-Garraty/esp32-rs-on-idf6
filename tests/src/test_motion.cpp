#include <catch2/catch_test_macros.hpp>
#include <array>
#include <memory_resource>

#include "domain/motion.hpp"

using namespace ecotiter::domain;

TEST_CASE("computeRamp: empty steps returns empty vector", "[motion]") {
    RampConfig config{10, 10, 100, 1000};
    auto ramp = computeRamp(0, config);
    REQUIRE(ramp.empty());
}

TEST_CASE("computeRamp: accel only (decelSteps = 0)", "[motion]") {
    RampConfig config{10, 0, 100, 1000};
    auto ramp = computeRamp(20, config);
    REQUIRE(ramp.size() == 20);
    // First 10 steps: decreasing from 1000 to ~100
    REQUIRE(ramp.front() == 1000);
    REQUIRE(ramp[9] <= ramp[0]);
    // Last 10 steps: cruise at minIntervalUs
    for (size_t i = 10; i < 20; i++) {
        REQUIRE(ramp[i] == 100);
    }
}

TEST_CASE("computeRamp: decel only (accelSteps = 0)", "[motion]") {
    RampConfig config{0, 10, 100, 1000};
    auto ramp = computeRamp(20, config);
    REQUIRE(ramp.size() == 20);
    // First 10 steps: cruise at minIntervalUs
    for (size_t i = 0; i < 10; i++) {
        REQUIRE(ramp[i] == 100);
    }
    // Last 10 steps: increasing from 100 to ~1000
    REQUIRE(ramp[10] == 100);
    REQUIRE(ramp.back() >= ramp[10]);
}

TEST_CASE("computeRamp: full trapezoid (accel + cruise + decel)", "[motion]") {
    RampConfig config{30, 30, 200, 2000};
    auto ramp = computeRamp(100, config);
    REQUIRE(ramp.size() == 100);
    // 30 accel + 40 cruise + 30 decel
    REQUIRE(ramp.front() == 2000);
    // Cruise phase at minIntervalUs
    for (size_t i = 30; i < 70; i++) {
        REQUIRE(ramp[i] == 200);
    }
}

TEST_CASE("computeRamp: accel + decel > total steps (scaling)", "[motion]") {
    // accelSteps + decelSteps = 30 > totalSteps = 20
    RampConfig config{15, 15, 100, 1000};
    auto ramp = computeRamp(20, config);
    REQUIRE(ramp.size() == 20);
    // After scaling, accel + decel should equal 20
    // Each phase should have some steps
    bool hasAccel = ramp.front() > ramp.back();
    bool hasDecel = ramp.front() < ramp.back();
    // Since scale = 20/30 = 0.667, accel = decel = 10, all steps used
    // First should be maxIntervalUs
    REQUIRE(ramp.front() == 1000);
    // Last should be near maxIntervalUs
    REQUIRE(ramp.back() >= 100);
    REQUIRE(ramp.back() <= 1000);
}

TEST_CASE("computeRamp: single step", "[motion]") {
    RampConfig config{10, 10, 100, 1000};
    auto ramp = computeRamp(1, config);
    REQUIRE(ramp.size() == 1);
}

TEST_CASE("computeRamp: first interval is maxIntervalUs", "[motion]") {
    RampConfig config{50, 50, 100, 1000};
    auto ramp = computeRamp(150, config);
    // sqrt(0/accel) = 0, so first interval = maxIntervalUs exactly
    REQUIRE(ramp.front() == config.maxIntervalUs);
}

TEST_CASE("computeRamp: last interval near maxIntervalUs", "[motion]") {
    RampConfig config{50, 50, 100, 1000};
    auto ramp = computeRamp(150, config);
    // Last decel interval approaches maxIntervalUs
    REQUIRE(ramp.back() > config.minIntervalUs);
    REQUIRE(ramp.back() <= config.maxIntervalUs);
}

TEST_CASE("computeRamp: cruise intervals are minIntervalUs", "[motion]") {
    RampConfig config{20, 20, 150, 1500};
    auto ramp = computeRamp(100, config);
    // 20 accel + 60 cruise + 20 decel
    for (size_t i = 20; i < 80; i++) {
        REQUIRE(ramp[i] == config.minIntervalUs);
    }
}

TEST_CASE("computeRamp: monotonic decreasing during accel", "[motion]") {
    RampConfig config{20, 20, 100, 1000};
    auto ramp = computeRamp(60, config);
    for (size_t i = 1; i < 20; i++) {
        REQUIRE(ramp[i] <= ramp[i - 1]);
    }
}

TEST_CASE("computeRamp: monotonic increasing during decel", "[motion]") {
    RampConfig config{20, 20, 100, 1000};
    auto ramp = computeRamp(60, config);
    // decel starts at index 40 (20 accel + 20 cruise)
    for (size_t i = 41; i < 60; i++) {
        REQUIRE(ramp[i] >= ramp[i - 1]);
    }
}

TEST_CASE("computeRamp: custom memory resource", "[motion]") {
    std::array<std::byte, 4096> buffer{};
    std::pmr::monotonic_buffer_resource mbr{buffer.data(), buffer.size(), std::pmr::null_memory_resource()};

    RampConfig config{10, 10, 100, 1000};
    auto ramp = computeRamp(30, config, &mbr);
    REQUIRE(ramp.size() == 30);
    REQUIRE(ramp.get_allocator().resource() == &mbr);
}
