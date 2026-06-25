#include "catch_amalgamated.hpp"
/*
 * test_periodic_timer.cc — PeriodicTimer unit tests
 *
 * Tests Start/Stop lifecycle, Schedule/Cancel, callback invocation count.
 */
#include <atomic>
#include <chrono>
#include <thread>

#include "util/PeriodicTimer.h"

using namespace cosmo;

TEST_CASE("PeriodicTimer: construction and destruction", "[periodic-timer]") {
    REQUIRE_NOTHROW([]() { PeriodicTimer timer("test-timer"); }());
}

TEST_CASE("PeriodicTimer: Start then Destroy lifecycle", "[periodic-timer]") {
    PeriodicTimer timer("lifecycle-test");

    SECTION("Start then Destroy does not crash") {
        REQUIRE_NOTHROW(timer.Start());
        REQUIRE_NOTHROW(timer.Destroy());
    }

    SECTION("Double Destroy is safe") {
        timer.Start();
        REQUIRE_NOTHROW(timer.Destroy());
        REQUIRE_NOTHROW(timer.Destroy());
    }

    SECTION("Destroy without Start is safe") {
        REQUIRE_NOTHROW(timer.Destroy());
    }
}

TEST_CASE("PeriodicTimer: Schedule returns valid TaskId", "[periodic-timer]") {
    PeriodicTimer timer("schedule-test");
    timer.Start();

    auto id = timer.Schedule([]() {}, 1000);
    REQUIRE(id != kInvalidTaskId);

    timer.Destroy();
}

TEST_CASE("PeriodicTimer: Cancel removes task", "[periodic-timer]") {
    PeriodicTimer timer("cancel-test");
    timer.Start();

    std::atomic<int> count{0};
    auto id = timer.Schedule([&]() { count++; }, 10);

    // Let it tick once
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    timer.Cancel(id);
    int countAfterCancel = count.load();

    // Wait more — count should not increase much after cancel
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    REQUIRE(count.load() <= countAfterCancel + 1);
    timer.Destroy();
}

TEST_CASE("PeriodicTimer: repeated callback fires multiple times", "[periodic-timer]") {
    PeriodicTimer timer("repeat-test");
    timer.Start();

    std::atomic<int> count{0};
    timer.Schedule([&]() { count++; }, 10, true);

    // Wait enough for several ticks
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    timer.Destroy();

    REQUIRE(count.load() >= 3);
}

TEST_CASE("PeriodicTimer: one-shot callback fires once", "[periodic-timer]") {
    PeriodicTimer timer("oneshot-test");
    timer.Start();

    std::atomic<int> count{0};
    timer.Schedule([&]() { count++; }, 10, false);

    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    timer.Destroy();

    REQUIRE(count.load() == 1);
}

TEST_CASE("PeriodicTimer: Cancel with invalid ID is safe", "[periodic-timer]") {
    PeriodicTimer timer("invalid-cancel");
    timer.Start();
    REQUIRE_NOTHROW(timer.Cancel(kInvalidTaskId));
    REQUIRE_NOTHROW(timer.Cancel(999999));
    timer.Destroy();
}
