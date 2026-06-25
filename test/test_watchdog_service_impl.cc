#include "catch_amalgamated.hpp"
/*
 * test_watchdog_service_impl.cc — WatchDogServiceImpl unit tests (DEBT-T01)
 *
 * Strategy: WatchDogServiceImpl wraps /dev/watchdog hardware timer.
 * Construction is safe anywhere (just creates the wrapper object).
 * Start/Stop require the actual device, tagged [.device].
 */
#include "mock/MockServiceRegistry.h"
#include "service/system/impl/WatchDogServiceImpl.h"

using namespace cosmo::service;

TEST_CASE("WatchDogServiceImpl: construction and destruction", "[WatchDogService]") {
    REQUIRE_NOTHROW([]() { WatchDogServiceImpl sut; }());
}

TEST_CASE("WatchDogServiceImpl: Start and Stop", "[WatchDogService][.device]") {
    WatchDogServiceImpl sut;

    SECTION("Start returns true on device") {
        REQUIRE(sut.Start() == true);
        sut.Stop();
    }

    SECTION("Stop returns true after start") {
        sut.Start();
        REQUIRE(sut.Stop() == true);
    }

    SECTION("Stop without start is safe") {
        REQUIRE_NOTHROW(sut.Stop());
    }

    SECTION("Double stop is safe") {
        sut.Start();
        sut.Stop();
        REQUIRE_NOTHROW(sut.Stop());
    }
}
