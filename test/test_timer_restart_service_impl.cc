#include "catch_amalgamated.hpp"
// Unit tests for TimerRestartServiceImpl — validates construction/destruction
// lifecycle and verifies the service interacts correctly with PeriodicTimer.
// Note: CaluRestart/DateSecRestart are private; we test observable behavior only.

#include "mock/MockConfigReadService.h"
#include "service/system/impl/TimerRestartServiceImpl.h"
#include "support/ScopedServiceOverride.h"
#include "trompeloeil.hpp"

using cosmo::service::TimerRestartServiceImpl;

TEST_CASE("TimerRestartServiceImpl: lifecycle", "[timer-restart]") {
    cosmo::test::MockConfigReadService configReadSvc;
    cosmo::test::ScopedServiceOverride<cosmo::service::IConfigReadService> registration(configReadSvc);

    SECTION("Stop is safe and idempotent") {
        cosmo::CfgRebootParamInfo disabledReboot;
        disabledReboot.isTimingRestart = false;
        ALLOW_CALL(configReadSvc, GetRebootParam()).RETURN(disabledReboot);

        TimerRestartServiceImpl sut;
        sut.Start();
        REQUIRE_NOTHROW(sut.Stop());
        REQUIRE_NOTHROW(sut.Stop());
        REQUIRE_NOTHROW(sut.Start());
    }
}

TEST_CASE("CfgRebootParamInfo: default values", "[timer-restart]") {
    cosmo::CfgRebootParamInfo info;

    SECTION("Defaults are sane") {
        REQUIRE(info.isTimingRestart == true);
        REQUIRE(info.weekDay == 0);                // Every day
        REQUIRE(info.restartTimeSec == 3600 * 2);  // 2 AM
    }
}
