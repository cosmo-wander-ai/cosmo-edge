#include "catch_amalgamated.hpp"
/*
 * test_alg_data_queue.cc - AlgDataQueue 单元测试
 */
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

TEST_CASE("AlgDataQueue concurrent access", "[AlgDataQueue]") {
    AlgDataQueue<int> queue("concurrent_q", 100);
    const int N = 50;

    std::thread producer([&]() {
        for (int i = 0; i < N; i++) {
            queue.Insert(i);
        }
    });

    std::thread consumer([&]() {
        int count = 0;
        while (count < N) {
            auto val = queue.Pop();
            if (val != 0 || queue.RestSize() > 0) {
                count++;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });

    producer.join();
    consumer.join();

    // Should not crash — that's the main assertion for concurrency
    REQUIRE(true);
}
