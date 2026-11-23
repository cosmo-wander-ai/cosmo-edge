#include "catch_amalgamated.hpp"
/*
 * test_alg_data_queue.cc - AlgDataQueue 单元测试
 */
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "flow/common/AlgDataQueue.h"

using namespace cosmo;

TEST_CASE("AlgDataQueue Insert and Pop", "[AlgDataQueue]") {
    AlgDataQueue<int> queue("test_q", 5);

    SECTION("Insert and Pop in order") {
        REQUIRE(queue.Insert(1));
        REQUIRE(queue.Insert(2));
        REQUIRE(queue.Insert(3));

        REQUIRE(queue.Pop() == 1);
        REQUIRE(queue.Pop() == 2);
        REQUIRE(queue.Pop() == 3);
    }

    SECTION("Pop from empty queue returns default") {
        auto val = queue.Pop();
        REQUIRE(val == 0);
    }

    SECTION("IsRunning initially true") {
        REQUIRE(queue.IsRunning());
    }
}

TEST_CASE("AlgDataQueue full queue behavior", "[AlgDataQueue]") {
    AlgDataQueue<int> queue("full_q", 3);

    SECTION("Non-force insert fails when full") {
        REQUIRE(queue.Insert(1));
        REQUIRE(queue.Insert(2));
        REQUIRE(queue.Insert(3));
        REQUIRE_FALSE(queue.Insert(4, false));

        // First element should still be 1
        REQUIRE(queue.Pop() == 1);
    }

    SECTION("Force insert discards head when full") {
        REQUIRE(queue.Insert(1));
        REQUIRE(queue.Insert(2));
        REQUIRE(queue.Insert(3));
        REQUIRE(queue.Insert(4, true));  // head popped

        REQUIRE(queue.Pop() == 2);
        REQUIRE(queue.Pop() == 3);
        REQUIRE(queue.Pop() == 4);
    }
}

TEST_CASE("AlgDataQueue Stop behavior", "[AlgDataQueue]") {
    AlgDataQueue<int> queue("stop_q", 5);

    queue.Insert(1);
    queue.Stop();

    SECTION("Insert after Stop returns false") {
        REQUIRE_FALSE(queue.Insert(2));
    }

    SECTION("IsRunning returns false after Stop") {
        REQUIRE_FALSE(queue.IsRunning());
    }

    SECTION("Pop after Stop returns default") {
        REQUIRE(queue.Pop() == 0);
    }
}

TEST_CASE("AlgDataQueue Status reporting", "[AlgDataQueue]") {
    AlgDataQueue<int> queue("status_q", 5);

    queue.Insert(10);
    queue.Insert(20);
    queue.Pop();

    AlgDataQueueInfo info;
    REQUIRE(queue.Status(info));

    REQUIRE(info.name == "status_q");
    REQUIRE(info.queSize == 5);
    REQUIRE(info.status.insertCount == 2);
    REQUIRE(info.status.processCount == 1);
    REQUIRE(info.queLength == 1);
    REQUIRE(info.status.holdCount == 1);
    REQUIRE(info.status.holdCountMax == 2);
}

TEST_CASE("AlgDataQueue current hold count follows dequeue and stop", "[AlgDataQueue]") {
    AlgDataQueue<int> queue("hold_count_q", 2);
    auto check = [&](size_t expected, size_t peak) {
        AlgDataQueueInfo info;
        REQUIRE(queue.Status(info));
        CHECK(info.queLength == expected);
        CHECK(info.status.holdCount == expected);
        CHECK(info.status.holdCountMax == peak);
    };

    check(0, 0);
    REQUIRE(queue.Insert(1));
    REQUIRE(queue.Insert(2));
    CHECK_FALSE(queue.Insert(3));
    check(2, 2);
    REQUIRE(queue.Insert(3, true));
    check(2, 2);
    CHECK(queue.Pop() == 2);
    check(1, 2);
    CHECK(queue.Pop() == 3);
    CHECK(queue.Pop() == 0);
    check(0, 2);
    REQUIRE(queue.Insert(4));
    queue.Stop();
    queue.Resume();
    check(0, 2);
}

TEST_CASE("AlgDataQueue WaitForData timeout", "[AlgDataQueue]") {
    AlgDataQueue<int> queue("wait_q", 5);

    SECTION("WaitForData returns false on empty queue timeout") {
        REQUIRE_FALSE(queue.WaitForData(50));
    }

    SECTION("WaitForData returns true when data available") {
        queue.Insert(1);
        REQUIRE(queue.WaitForData(50));
    }
}

TEST_CASE("AlgDataQueue SetMaxSize", "[AlgDataQueue]") {
    AlgDataQueue<int> queue("resize_q", 2);

    queue.Insert(1);
    queue.Insert(2);
    REQUIRE_FALSE(queue.Insert(3));

    queue.SetMaxSize(5);
    REQUIRE(queue.Insert(3));
    REQUIRE(queue.Insert(4));
}

TEST_CASE("AlgDataQueue RecordDiscard", "[AlgDataQueue]") {
    AlgDataQueue<int> queue("discard_q", 5);
    queue.Insert(1);
    queue.RecordDiscard();

    AlgDataQueueInfo info;
    queue.Status(info);
    REQUIRE(info.status.discardCount == 1);
}

TEST_CASE("AlgDataQueue reports immediate insert capacity", "[AlgDataQueue]") {
    AlgDataQueue<int> queue("capacity_q", 1);
    REQUIRE(queue.CanAccept());
    REQUIRE(queue.Insert(1));
    CHECK_FALSE(queue.CanAccept());
    CHECK(queue.Pop() == 1);
    CHECK(queue.CanAccept());
}

TEST_CASE("AlgDataQueue concurrent access", "[AlgDataQueue]") {
    AlgDataQueue<int> queue("concurrent_q", 100);
    const int N  = 50;
    int consumed = 0;

    std::thread producer([&]() {
        for (int i = 1; i <= N; i++) {
            queue.Insert(i);
        }
    });

    std::thread consumer([&]() {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (consumed < N && std::chrono::steady_clock::now() < deadline) {
            auto val = queue.Pop();
            if (val != 0) {
                consumed++;
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
    });

    producer.join();
    consumer.join();

    REQUIRE(consumed == N);
}

TEST_CASE("AlgDataQueue snapshot reports current hold during concurrent access", "[AlgDataQueue]") {
    AlgDataQueue<int> queue("snapshot_q", 20);
    std::atomic<bool> started{false};
    std::atomic<bool> done{false};
    std::thread producer([&]() {
        while (!started.load())
            std::this_thread::yield();
        for (int i = 0; i < 5000; ++i)
            queue.Insert(i + 1, true);
        done.store(true);
    });
    std::thread consumer([&]() {
        while (!started.load())
            std::this_thread::yield();
        while (!done.load() || queue.RestSize() != 0)
            queue.Pop();
    });
    started.store(true);
    bool consistent = true;
    for (int i = 0; i < 5000; ++i) {
        AlgDataQueueInfo status;
        if (!queue.Status(status) || status.status.holdCount != status.queLength)
            consistent = false;
    }
    producer.join();
    consumer.join();
    CHECK(consistent);
    AlgDataQueueInfo final_status;
    REQUIRE(queue.Status(final_status));
    CHECK(final_status.queLength == 0);
    CHECK(final_status.status.holdCount == 0);
}
