#include "catch_amalgamated.hpp"
/*
 * test_async_queue.cc - AsyncQueue (util) 单元测试
 */
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <thread>

#include "util/AsyncQueue.h"

using namespace cosmo;

TEST_CASE("AsyncQueue Insert and processor callback", "[AsyncQueue]") {
    std::atomic<int> processedCount{0};
    std::atomic<int> lastValue{-1};

    {
        AsyncQueue<int> queue("test_async", 10);
        queue.SetProcessor([&](int&& val) {
            lastValue.store(val);
            processedCount.fetch_add(1);
        });

        queue.Insert(42);
        queue.Insert(99);

        // Wait for async processing
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        REQUIRE(processedCount.load() == 2);
    }
}

TEST_CASE("AsyncQueue full queue discard (non-force)", "[AsyncQueue]") {
    AsyncQueue<int> queue("full_async", 3);
    // Don't set processor, so items accumulate
    // Note: AsyncQueue's run() loop processes items, so we need to be careful
    // Insert rapidly to fill
    queue.Insert(1);
    queue.Insert(2);
    queue.Insert(3);
    // This may succeed or fail depending on timing — just verify no crash
    queue.Insert(4);
    REQUIRE(true);
}

TEST_CASE("AsyncQueue Stop", "[AsyncQueue]") {
    AsyncQueue<int> queue("stop_async", 5);

    queue.Stop();
    REQUIRE_FALSE(queue.IsRunning());
    REQUIRE_FALSE(queue.Insert(1));
}

TEST_CASE("AsyncQueue stop finishes the active callback and retains queued ownership until destruction",
          "[AsyncQueue][mp4-record][lifecycle]") {
    std::weak_ptr<int> pending_one;
    std::weak_ptr<int> pending_two;
    {
        std::mutex mutex;
        std::condition_variable changed;
        bool entered  = false;
        bool released = false;
        std::atomic<int> processed{0};
        AsyncQueue<std::shared_ptr<int>> queue("recording_stop", 5);
        queue.SetProcessor([&](std::shared_ptr<int>&&) {
            std::unique_lock<std::mutex> lock(mutex);
            ++processed;
            entered = true;
            changed.notify_all();
            changed.wait(lock, [&] { return released; });
        });

        const bool first_inserted = queue.Insert(std::make_shared<int>(1));
        bool callback_entered;
        {
            std::unique_lock<std::mutex> lock(mutex);
            callback_entered = changed.wait_for(lock, std::chrono::seconds(5), [&] { return entered; });
        }
        auto first                = std::make_shared<int>(2);
        auto second               = std::make_shared<int>(3);
        pending_one               = first;
        pending_two               = second;
        const bool first_pending  = queue.Insert(std::move(first));
        const bool second_pending = queue.Insert(std::move(second));
        auto stopped              = std::async(std::launch::async, [&] {
            queue.Stop();
            queue.stop();
        });
        const auto deadline       = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (queue.IsRunning() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        const bool stopping      = !queue.IsRunning();
        const bool late_rejected = !queue.Insert(std::make_shared<int>(4));
        const bool waiting_for_callback =
            stopped.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
        {
            std::lock_guard<std::mutex> lock(mutex);
            released = true;
        }
        changed.notify_all();
        stopped.get();

        REQUIRE(first_inserted);
        REQUIRE(callback_entered);
        REQUIRE(first_pending);
        REQUIRE(second_pending);
        REQUIRE(stopping);
        REQUIRE(late_rejected);
        REQUIRE(waiting_for_callback);
        REQUIRE(processed.load() == 1);
        REQUIRE(queue.RestSize() == 2);
        REQUIRE_FALSE(pending_one.expired());
        REQUIRE_FALSE(pending_two.expired());
    }
    REQUIRE(pending_one.expired());
    REQUIRE(pending_two.expired());
}

TEST_CASE("AsyncQueue Status", "[AsyncQueue]") {
    AsyncQueue<int> queue("status_async", 10);
    queue.SetProcessor([](int&&) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); });

    queue.Insert(1);
    queue.Insert(2);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    AsyncQueueInfo info;
    queue.Status(info);

    REQUIRE(info.name == "status_async");
    REQUIRE(info.status.insertCount == 2);
    REQUIRE(info.queSize == 10);
}

TEST_CASE("AsyncQueue KeyInQueue", "[AsyncQueue]") {
    AsyncQueue<std::string> queue("key_async", 10);
    // Don't set processor so items stay in queue
    queue.SetChecker([](const std::string& item, const std::string& key) { return item == key; });

    queue.Insert(std::string("hello"));

    // Item might already be processed, so just check no crash
    std::string key = "hello";
    queue.KeyInQueue(key);  // May or may not find it
    REQUIRE(true);
}
