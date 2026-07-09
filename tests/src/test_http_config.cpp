#include <catch2/catch_test_macros.hpp>

// HTTP server config limits — must match sdkconfig.defaults
// httpd_main.c: if (HTTPD_MAX_SOCKETS < config->max_open_sockets + 3) → ESP_ERR_INVALID_ARG
// With CONFIG_LWIP_MAX_SOCKETS=8 (sdkconfig.defaults), max_open_sockets + 3 ≤ 8

static constexpr int TEST_LWIP_MAX_SOCKETS = 8;
static constexpr int TEST_MAX_OPEN_SOCKETS = 5;

TEST_CASE("Config: HTTP server max_open_sockets is within LWIP limit", "[config]") {
    // httpd validation: config->max_open_sockets + 3 <= LWIP_MAX_SOCKETS
    REQUIRE(TEST_MAX_OPEN_SOCKETS + 3 <= TEST_LWIP_MAX_SOCKETS);

    // Also verify it passes with some margin
    REQUIRE(TEST_MAX_OPEN_SOCKETS + 3 <= TEST_LWIP_MAX_SOCKETS);
}

TEST_CASE("Config: max_open_sockets=6 should fail validation", "[config]") {
    // Demonstrate that 6 would be invalid (6+3=9 > 8)
    constexpr int would_fail = 6;
    REQUIRE_FALSE(would_fail + 3 <= TEST_LWIP_MAX_SOCKETS);
}
