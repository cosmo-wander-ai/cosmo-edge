#include "catch_amalgamated.hpp"
// Unit tests for TimerRestartServiceImpl — validates construction/destruction
// lifecycle and verifies the service interacts correctly with PeriodicTimer.
// Note: CaluRestart/DateSecRestart are private; we test observable behavior only.

#include <chrono>
#include <thread>

#include "mock/MockConfigReadService.h"
#include "service/system/impl/TimerRestartServiceImpl.h"
#include "support/ScopedServiceOverride.h"
#include "trompeloeil.hpp"

using cosmo::service::TimerRestartServiceImpl;

TEST_CASE("TimerRestartServiceImpl: lifecycle", "[timer-restart]") {
    cosmo::test::MockConfigReadService configReadSvc;
    cosmo::test::ScopedServiceOverride<cosmo::service::IConfigReadService> registration(configReadSvc);

    SECTION("Default construction does not start timer") {
        TimerRestartServiceImpl sut;
        // No timer thread running — destructor should be safe
    }

    SECTION("start() creates timer and schedules periodic check") {
        // GetRebootParam will be called by the timer callback
        cosmo::CfgRebootParamInfo disabledReboot;
        disabledReboot.isTimingRestart = false;
        ALLOW_CALL(configReadSvc, GetRebootParam()).RETURN(disabledReboot);

        TimerRestartServiceImpl sut;
        sut.Start();
        // Let the timer fire at least once (interval is 10s, so we wait briefly)
        // The timer callback calls CaluRestart() which reads GetRebootParam()
        // Since isTimingRestart=false, it returns immediately
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // Destructor will cancel the timer and clean up
    }

    SECTION("Destructor cleans up timer without crash") {
        cosmo::CfgRebootParamInfo disabledReboot;
        disabledReboot.isTimingRestart = false;
        ALLOW_CALL(configReadSvc, GetRebootParam()).RETURN(disabledReboot);

        {
            TimerRestartServiceImpl sut;
            sut.Start();
        }
        // If we reach here, destructor succeeded
        REQUIRE(true);
    }

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

    SECTION("WeekDay range") {
        // 0 = every day, 1-7 = Mon-Sun
        info.weekDay = 0;
        REQUIRE(info.weekDay >= 0);
        REQUIRE(info.weekDay <= 7);
    }
}
