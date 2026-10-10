#include "catch_amalgamated.hpp"
/*
 * test_periodic_timer.cc — PeriodicTimer unit tests
 *
 * Tests Start/Stop lifecycle, Schedule/Cancel, callback invocation count.
 */
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <stdexcept>
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

TEST_CASE("PeriodicTimer isolates exceptions without changing task repetition",
          "[periodic-timer][callback-exception]") {
    const bool unknown_exception = GENERATE(false, true);
    std::mutex mutex;
    std::condition_variable changed;
    int one_shot_failures     = 0;
    int ready_batch_successes = 0;
    int repeated_failures     = 0;
    int repeated_successes    = 0;
    const auto fail           = [unknown_exception] {
        if (unknown_exception) {
            throw 42;
        }
        throw std::runtime_error("expected timer callback failure");
    };
    PeriodicTimer timer("callback-isolation");

    // Both one-shot tasks are due before Start, so they run in the same batch.
    timer.Schedule(
        [&] {
            ++one_shot_failures;
            fail();
        },
        0, false);
    timer.Schedule([&] { ++ready_batch_successes; }, 0, false);
    timer.Schedule(
        [&] {
            {
                std::lock_guard<std::mutex> lock(mutex);
                ++repeated_failures;
                changed.notify_all();
            }
            fail();
        },
        10);
    timer.Schedule(
        [&] {
            std::lock_guard<std::mutex> lock(mutex);
            ++repeated_successes;
            changed.notify_all();
        },
        10);
    timer.Start();

    bool kept_running;
    {
        std::unique_lock<std::mutex> lock(mutex);
        kept_running = changed.wait_for(lock, std::chrono::seconds(3),
                                        [&] { return repeated_failures >= 3 && repeated_successes >= 3; });
    }
    timer.Destroy();

    CHECK(kept_running);
    CHECK(one_shot_failures == 1);
    CHECK(ready_batch_successes == 1);
    CHECK(repeated_failures >= 3);
    CHECK(repeated_successes >= 3);
}

TEST_CASE("PeriodicTimer Destroy joins an active callback that throws",
          "[periodic-timer][callback-exception]") {
    const bool unknown_exception = GENERATE(false, true);
    std::promise<void> entered;
    std::promise<void> release;
    std::promise<void> destroying;
    auto entered_future    = entered.get_future();
    auto released          = release.get_future().share();
    auto destroying_future = destroying.get_future();
    PeriodicTimer timer("stop-failing-callback");
    timer.Schedule(
        [&] {
            entered.set_value();
            released.wait();
            if (unknown_exception) {
                throw 42;
            }
            throw std::runtime_error("expected callback failure during stop");
        },
        0, false);
    timer.Start();

    if (entered_future.wait_for(std::chrono::seconds(3)) != std::future_status::ready) {
        release.set_value();
        timer.Destroy();
        FAIL("timer callback did not start");
    }
    auto stopped                    = std::async(std::launch::async, [&] {
        destroying.set_value();
        timer.Destroy();
    });
    const auto destroy_started      = destroying_future.wait_for(std::chrono::seconds(3));
    const auto waiting_for_callback = stopped.wait_for(std::chrono::milliseconds(20));
    // Release the callback before assertions so failure cannot strand the joining thread.
    release.set_value();
    const auto joined = stopped.wait_for(std::chrono::seconds(3));
    stopped.get();

    CHECK(destroy_started == std::future_status::ready);
    CHECK(waiting_for_callback == std::future_status::timeout);
    CHECK(joined == std::future_status::ready);
}
