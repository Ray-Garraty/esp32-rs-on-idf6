#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "domain/ols.hpp"
#include "domain/z_factor.hpp"

using namespace ecotiter::domain;

static constexpr float TOL = 0.0001f;
static bool approx(float a, float b) { return std::fabs(a - b) < TOL; }

// ── Z-factor tests ───────────────────────────────────────────────

TEST_CASE("z_factor: exact match at table vertex 25C 101.3kPa", "[z_factor]") {
  float z = getZFactor(25.0f, 101.3f);
  REQUIRE(approx(z, 1.0041f));
}

TEST_CASE("z_factor: exact match at table vertex 15C 80kPa", "[z_factor]") {
  float z = getZFactor(15.0f, 80.0f);
  REQUIRE(approx(z, 1.0018f));
}

TEST_CASE("z_factor: exact match at table vertex 30C 106.7kPa", "[z_factor]") {
  float z = getZFactor(30.0f, 106.7f);
  REQUIRE(approx(z, 1.0055f));
}

TEST_CASE("z_factor: interpolation at midpoint", "[z_factor]") {
  // Between 25.0C (1.0041) and 25.5C (1.0042) at 101.3kPa
  // At 25.25C, expect ~1.00415
  float z = getZFactor(25.25f, 101.3f);
  REQUIRE(z > 1.0040f);
  REQUIRE(z < 1.0043f);
}

TEST_CASE("z_factor: interpolation between pressures", "[z_factor]") {
  // At 25.0C: 101.3kPa=1.0041, 106.7kPa=1.0042
  // At ~104.0kPa, expect ~1.00414
  float z = getZFactor(25.0f, 104.0f);
  REQUIRE(z > 1.0040f);
  REQUIRE(z < 1.0043f);
}

TEST_CASE("z_factor: clamps temperature below 15C", "[z_factor]") {
  float z = getZFactor(10.0f, 101.3f);
  REQUIRE(approx(z, getZFactor(15.0f, 101.3f)));
}

TEST_CASE("z_factor: clamps temperature above 30C", "[z_factor]") {
  float z = getZFactor(35.0f, 101.3f);
  REQUIRE(approx(z, getZFactor(30.0f, 101.3f)));
}

TEST_CASE("z_factor: clamps pressure below 80kPa", "[z_factor]") {
  float z = getZFactor(25.0f, 70.0f);
  REQUIRE(approx(z, getZFactor(25.0f, 80.0f)));
}

TEST_CASE("z_factor: clamps pressure above 106.7kPa", "[z_factor]") {
  float z = getZFactor(25.0f, 120.0f);
  REQUIRE(approx(z, getZFactor(25.0f, 106.7f)));
}

// ── calculateNewStepsPerMl tests ─────────────────────────────────

TEST_CASE("calcNewSPM: nominal case", "[z_factor]") {
  // 7730 * 8.14 / 8.20 ≈ 7673.5
  float spm = calculateNewStepsPerMl(7730.0f, 8.14f, 8.20f);
  REQUIRE(spm < 7730.0f);
  REQUIRE(spm > 7600.0f);
}

TEST_CASE("calcNewSPM: actual volume same as target", "[z_factor]") {
  float spm = calculateNewStepsPerMl(7730.0f, 8.14f, 8.14f);
  REQUIRE(approx(spm, 7730.0f));
}

TEST_CASE("calcNewSPM: guards against zero actual volume", "[z_factor]") {
  float spm = calculateNewStepsPerMl(7730.0f, 8.14f, 0.0f);
  REQUIRE(approx(spm, 7730.0f));
}

// ── OLS tests ────────────────────────────────────────────────────

TEST_CASE("ols: perfect linear fit returns k and R²=1", "[ols]") {
  // y = 0.03052 * x
  float freqs[] = {100.0f, 500.0f, 1000.0f, 2000.0f};
  float speeds[] = {3.052f, 15.26f, 30.52f, 61.04f};
  auto result = calculateSpeedCalibration(freqs, speeds, 4);
  REQUIRE(std::fabs(result.k - 0.03052f) < 0.001f);
  REQUIRE(result.rSquared > 0.999f);
}

TEST_CASE("ols: insufficient points returns zero", "[ols]") {
  float freqs[] = {100.0f};
  float speeds[] = {3.052f};
  auto result = calculateSpeedCalibration(freqs, speeds, 1);
  REQUIRE(std::fabs(result.k) < 0.0001f);
  REQUIRE(std::fabs(result.rSquared) < 0.0001f);
}

TEST_CASE("ols: zero points returns zero", "[ols]") {
  auto result = calculateSpeedCalibration(nullptr, nullptr, 0);
  REQUIRE(std::fabs(result.k) < 0.0001f);
  REQUIRE(std::fabs(result.rSquared) < 0.0001f);
}

TEST_CASE("ols: known dataset gives expected result", "[ols]") {
  // Simple case: f={100,200}, v={1.0,2.0} → k=0.01, R²=1.0
  float freqs[] = {100.0f, 200.0f};
  float speeds[] = {1.0f, 2.0f};
  auto result = calculateSpeedCalibration(freqs, speeds, 2);
  REQUIRE(std::fabs(result.k - 0.01f) < 0.0001f);
  REQUIRE(result.rSquared > 0.999f);
}

TEST_CASE("ols: three points with small error", "[ols]") {
  // f={50,100,150}, v={2.0,4.1,5.9} → k≈0.039, R²≈0.997
  float freqs[] = {50.0f, 100.0f, 150.0f};
  float speeds[] = {2.0f, 4.1f, 5.9f};
  auto result = calculateSpeedCalibration(freqs, speeds, 3);
  REQUIRE(std::fabs(result.k - 0.039f) < 0.005f);
  REQUIRE(result.rSquared > 0.99f);
}

TEST_CASE("ols: handles all same frequencies (zero denominator)", "[ols]") {
  float freqs[] = {100.0f, 100.0f, 100.0f};
  float speeds[] = {1.0f, 2.0f, 3.0f};
  auto result = calculateSpeedCalibration(freqs, speeds, 3);
  REQUIRE(std::fabs(result.k) < 0.0001f);
  REQUIRE(std::fabs(result.rSquared) < 0.0001f);
}
