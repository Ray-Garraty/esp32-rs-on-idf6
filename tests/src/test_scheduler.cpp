#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>

#include "application/scheduler.hpp"

using namespace ecotiter::application;

TEST_CASE("TickScheduler: tick increments global tick", "[scheduler]") {
    gTick.store(0, std::memory_order_relaxed);
    TickScheduler sched;
    uint32_t before = gTick.load(std::memory_order_relaxed);
    sched.tick();
    uint32_t after = gTick.load(std::memory_order_relaxed);
    REQUIRE(after == before + 1);
}

TEST_CASE("TickScheduler: shouldBroadcast returns true every 30 ticks", "[scheduler]") {
    gTick.store(0, std::memory_order_relaxed);
    TickScheduler sched;
    // First broadcast fires at tick BROADCAST_INTERVAL (30) from 0
    for (int i = 0; i < 30; ++i) {
        REQUIRE(sched.shouldBroadcast() == false);
        sched.tick();
    }
    REQUIRE(sched.shouldBroadcast() == true);

    // Next broadcast at tick 60: 29 ticks false, then 30th tick true
    for (int i = 0; i < 29; ++i) {
        sched.tick();
        REQUIRE(sched.shouldBroadcast() == false);
    }
    sched.tick();
    REQUIRE(sched.shouldBroadcast() == true);
}

TEST_CASE("TickScheduler: handles 32-bit tick wrapping", "[scheduler]") {
    gTick.store(std::numeric_limits<uint32_t>::max() - 5,
                std::memory_order_relaxed);

    TickScheduler sched;
    // Advance past wrap (5 ticks to overflow, BROADCAST_INTERVAL to trigger)
    for (int i = 0; i < 35; ++i) {
        sched.tick();
    }
    // gTick ~= 29, shouldBroadcast needs BROADCAST_INTERVAL (30)
    REQUIRE(sched.shouldBroadcast() == false);

    sched.tick(); // gTick = 30
    REQUIRE(sched.shouldBroadcast() == true);
}
