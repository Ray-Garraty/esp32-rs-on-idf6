#include <catch2/catch_test_macros.hpp>

#include "mdns.h"

TEST_CASE("mDNS header reachable and API compiles", "[mdns]") {
    // Regression test: verify the mDNS stub header is reachable and the
    // function signatures used by WifiManager::startMdns() compile.
    // The original bug was a compilation error because mdns.h was not
    // available in the build system (moved to IDF Component Registry).

    esp_err_t err;

    err = mdns_init();
    REQUIRE(err == ESP_OK);

    err = mdns_hostname_set("ecotiter");
    REQUIRE(err == ESP_OK);

    err = mdns_service_add(
        "EcoTiter Burette Controller",
        "_http",
        "_tcp",
        80,
        nullptr,
        0);
    REQUIRE(err == ESP_OK);

    mdns_free();
}
