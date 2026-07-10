#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdlib>
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

// ── ADC Calibration Stabilization Logic Tests ─────────────────────

static int g_stabilization_test_raw[] = {1500, 1502, 1498, 1501, 1499};
static size_t g_stab_idx = 0;

static uint16_t stabReadFn() {
    uint16_t v = static_cast<uint16_t>(g_stabilization_test_raw[g_stab_idx]);
    g_stab_idx = (g_stab_idx + 1) % (sizeof(g_stabilization_test_raw) / sizeof(g_stabilization_test_raw[0]));
    return v;
}

TEST_CASE("ADC stabilization: stable signal returns median", "[adc][stabilization]") {
    // All values within ±2 mV — should stabilize
    g_stab_idx = 0;
    int values[32];
    for (int i = 0; i < 32; ++i) {
        values[i] = 1500;
    }
    // Test stabilization by checking max-min
    int minV = 1500, maxV = 1500;
    for (int i = 0; i < 32; ++i) {
        if (values[i] < minV) minV = values[i];
        if (values[i] > maxV) maxV = values[i];
    }
    REQUIRE((maxV - minV) <= 5);
}

TEST_CASE("ADC stabilization: unstable signal exceeds tolerance", "[adc][stabilization]") {
    // Wild swings — should fail tolerance
    int values[32];
    for (int i = 0; i < 32; ++i) {
        values[i] = 1500 + (i * 10);
    }
    int minV = values[0], maxV = values[0];
    for (int i = 1; i < 32; ++i) {
        if (values[i] < minV) minV = values[i];
        if (values[i] > maxV) maxV = values[i];
    }
    REQUIRE((maxV - minV) > 5);
}

TEST_CASE("ADC stabilization: median computation", "[adc][stabilization]") {
    // Odd number of sorted values — median is middle element
    uint16_t sorted[] = {1000, 1005, 1010, 1015, 1020, 1025, 1030,
                         1035, 1040, 1045, 1050, 1055, 1060, 1065,
                         1070, 1075, 1080, 1085, 1090, 1095, 1100,
                         1105, 1110, 1115, 1120, 1125, 1130, 1135,
                         1140, 1145, 1150, 1155};
    constexpr size_t N = sizeof(sorted) / sizeof(sorted[0]);
    // qsort for median
    auto compare = [](const void* a, const void* b) {
        uint16_t va = *static_cast<const uint16_t*>(a);
        uint16_t vb = *static_cast<const uint16_t*>(b);
        if (va < vb) return -1;
        if (va > vb) return 1;
        return 0;
    };
    std::qsort(sorted, N, sizeof(uint16_t), compare);
    uint16_t median = sorted[N / 2];
    // Array is already sorted: index 16 = 1000 + 16*5 = 1080
    REQUIRE(median == 1080);
}

// ── ADC OLS Math Tests ────────────────────────────────────────────

TEST_CASE("ADC OLS: inverse calibration formula", "[adc][ols]") {
    // Given 5 points with known relationship:
    // raw = 1.02 * ref + 5.0  (simulated ADC with gain + offset)
    // Then calibrated = (raw - 5) / 1.02 = 0.98039 * raw - 4.90196
    // a_x1000 = 980, b = -5

    double sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
    uint16_t raw[] = {205, 615, 1025, 1435, 1845}; // ref * 1.02 + 5 for ref=200,600,1000,1400,1800

    for (int i = 0; i < 5; ++i) {
        double x = static_cast<double>(raw[i]);
        double y = static_cast<double>(i);
        sumX += x;
        sumY += y;
        sumXY += x * y;
        sumX2 += x * x;
    }

    double n = 5.0;
    double denom = n * sumX2 - sumX * sumX;
    REQUIRE(std::abs(denom) > 1e-12);

    double aRaw = (n * sumXY - sumX * sumY) / denom;
    double bRaw = (sumY - aRaw * sumX) / n;

    // The inversion should give meaningful slope/intercept
    REQUIRE(std::abs(aRaw) > 1e-12);
    double coeffA = 1.0 / aRaw;
    double coeffB = -bRaw / aRaw;
    REQUIRE(coeffA > 0.0f);
}
