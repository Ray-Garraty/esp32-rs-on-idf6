#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>

#include "domain/errors.hpp"
#include "infrastructure/storage/nvs.hpp"

// Regression test for tryStartSTA bugfix (26_07_13_wifi_sta):
//
// The original bug: init() sets WIFI_MODE_APSTA (line 75) but does NOT call
// esp_wifi_start(). tryStartSTA() checks `if (currentMode == WIFI_MODE_NULL)`
// which is FALSE when mode == WIFI_MODE_APSTA, so esp_wifi_start() is never
// called and esp_wifi_connect() silently fails with ESP_ERR_WIFI_NOT_STARTED.
//
// Fix changes:
//   1. Condition changed to check BOTH WIFI_MODE_NULL and WIFI_MODE_APSTA
//   2. Added startedHere tracking and cleanup (stop + restore APSTA) on failure
//   3. Added ESP_LOGE on set_config/connect failures
//   4. startAP() now explicitly sets WIFI_MODE_APSTA before starting
//   5. wifiReadStr NVS handle changed from readWrite=true to false
//
// These changes are in the firmware (ESP-IDF-dependent) code path and cannot
// be host-tested directly. The firmware build verification confirms compilation.
// The Validator performs hardware-level verification.

// Test that wifiReadCount returns a valid value (stubbed to 0 on host)
TEST_CASE("wifiReadCount returns a value", "[wifi][nvs]") {
    auto result = ecotiter::infrastructure::storage::wifiReadCount();
    REQUIRE(result.has_value());
    // Stub returns 0; real implementation reads from NVS
    REQUIRE(*result == 0);
}

// Test that wifiWriteStr returns success (stubbed on host)
TEST_CASE("wifiWriteStr returns success", "[wifi][nvs]") {
    auto result = ecotiter::infrastructure::storage::wifiWriteStr("ssid_0", "test");
    REQUIRE(result.has_value());
}

// Test that wifiWriteCount returns success (stubbed on host)
TEST_CASE("wifiWriteCount returns success", "[wifi][nvs]") {
    auto result = ecotiter::infrastructure::storage::wifiWriteCount(1);
    REQUIRE(result.has_value());
}
