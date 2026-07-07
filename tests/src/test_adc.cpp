#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <atomic>
#include <algorithm>
#include <limits>

static std::atomic<uint16_t> gCoeffAX1000{1000};
static std::atomic<int16_t> gCoeffB{0};

static int16_t calibratedFromRaw(uint16_t raw) {
    int32_t a = gCoeffAX1000.load(std::memory_order_relaxed);
    int32_t b = gCoeffB.load(std::memory_order_relaxed);
    int32_t result = (a * static_cast<int32_t>(raw)) / 1000 + b;
    return static_cast<int16_t>(std::clamp(
        result,
        static_cast<int32_t>(std::numeric_limits<int16_t>::min()),
        static_cast<int32_t>(std::numeric_limits<int16_t>::max())));
}

TEST_CASE("ADC calibration defaults", "[adc]") {
    REQUIRE(gCoeffAX1000.load() == 1000);
    REQUIRE(gCoeffB.load() == 0);
}

TEST_CASE("ADC calibration with defaults", "[adc]") {
    // a=1000, b=0: calibrated = (1000*raw)/1000 + 0 = raw
    REQUIRE(calibratedFromRaw(0) == 0);
    REQUIRE(calibratedFromRaw(1000) == 1000);
    REQUIRE(calibratedFromRaw(2900) == 2900);
}

TEST_CASE("ADC calibration set and get", "[adc]") {
    gCoeffAX1000.store(500);
    gCoeffB.store(10);
    REQUIRE(gCoeffAX1000.load() == 500);
    REQUIRE(gCoeffB.load() == 10);

    // (500*1000)/1000 + 10 = 510
    REQUIRE(calibratedFromRaw(1000) == 510);

    gCoeffAX1000.store(1000);
    gCoeffB.store(0);
}

TEST_CASE("ADC calibration negative offset", "[adc]") {
    gCoeffB.store(-100);
    // (1000*1000)/1000 + (-100) = 900
    REQUIRE(calibratedFromRaw(1000) == 900);
    gCoeffB.store(0);
}

TEST_CASE("ADC calibration clamp to i16 max", "[adc]") {
    gCoeffAX1000.store(10923);
    // (10923*3000)/1000 = 32769 -> clamp to 32767
    REQUIRE(calibratedFromRaw(3000) == 32767);
    gCoeffAX1000.store(1000);
}

TEST_CASE("ADC calibration clamp to i16 min", "[adc]") {
    gCoeffB.store(-32768);
    // (1000*0)/1000 + (-32768) = -32768 -> clamp to -32768
    REQUIRE(calibratedFromRaw(0) == -32768);
    gCoeffB.store(0);
}
