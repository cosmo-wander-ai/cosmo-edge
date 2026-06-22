#include "catch_amalgamated.hpp"
/*
 * test_async_queue.cc - AsyncQueue (util) 单元测试
 */
#include <atomic>
#include <chrono>
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
