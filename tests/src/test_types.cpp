#include <catch2/catch_test_macros.hpp>
#include <domain/types.hpp>

using namespace ecotiter::domain;

TEST_CASE("Steps arithmetic", "[types]") {
    Steps a{100};
    Steps b{50};

    REQUIRE((a + b).value == 150);
    REQUIRE((a - b).value == 50);

    a += b;
    REQUIRE(a.value == 150);

    a -= b;
    REQUIRE(a.value == 100);
}

TEST_CASE("Steps comparison", "[types]") {
    REQUIRE(Steps{100} == Steps{100});
    REQUIRE(Steps{100} != Steps{101});
    REQUIRE(Steps{100} > Steps{50});
    REQUIRE(Steps{50} < Steps{100});
}

TEST_CASE("Direction enum", "[types]") {
    REQUIRE(static_cast<uint8_t>(Direction::Cw) == 0);
    REQUIRE(static_cast<uint8_t>(Direction::Ccw) == 1);
}
