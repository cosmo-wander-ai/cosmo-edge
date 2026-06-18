#include "catch_amalgamated.hpp"
/*
 * test_buffer_pool.cc - BufferPool unit tests
 *
 * Tests for cosmo::flow::BufferPool — acquire, release, pool sizing, and thread safety.
 */
#include <atomic>
#include <thread>
#include <vector>

#include "flow/channel/BufferPool.h"

using namespace cosmo::flow;

TEST_CASE("BufferPool: AcquireBuffer creates buffer with capacity", "[BufferPool]") {
    BufferPool pool(5);

    auto buf = pool.AcquireBuffer(1024);
    REQUIRE(buf != nullptr);
    REQUIRE(buf->capacity() >= 1024);
    REQUIRE(buf->empty());
}

TEST_CASE("BufferPool: ReleaseBuffer and reuse", "[BufferPool]") {
    BufferPool pool(5);

    auto buf = pool.AcquireBuffer(2048);
    REQUIRE(buf != nullptr);
    buf->push_back(0x42);
    REQUIRE(pool.GetPoolSize() == 0);

    pool.ReleaseBuffer(std::move(buf));
    REQUIRE(pool.GetPoolSize() == 1);

    // Reacquire — should reuse the pooled buffer
    auto buf2 = pool.AcquireBuffer(512);
    REQUIRE(buf2 != nullptr);
    REQUIRE(buf2->empty());             // Cleared on reuse
    REQUIRE(buf2->capacity() >= 2048);  // Preserves original capacity
    REQUIRE(pool.GetPoolSize() == 0);   // Pool drained
}

TEST_CASE("BufferPool: Reacquire with larger minSize expands capacity", "[BufferPool]") {
    BufferPool pool(5);

    auto buf = pool.AcquireBuffer(256);
    pool.ReleaseBuffer(std::move(buf));

    auto buf2 = pool.AcquireBuffer(4096);
    REQUIRE(buf2 != nullptr);
    REQUIRE(buf2->capacity() >= 4096);
}

TEST_CASE("BufferPool: Pool max size limits", "[BufferPool]") {
    BufferPool pool(2);

    auto buf1 = pool.AcquireBuffer(64);
    auto buf2 = pool.AcquireBuffer(64);
    auto buf3 = pool.AcquireBuffer(64);

    pool.ReleaseBuffer(std::move(buf1));
    pool.ReleaseBuffer(std::move(buf2));
    REQUIRE(pool.GetPoolSize() == 2);

    // Third release should be dropped (pool full)
    pool.ReleaseBuffer(std::move(buf3));
    REQUIRE(pool.GetPoolSize() == 2);
}

TEST_CASE("BufferPool: Release nullptr is safe", "[BufferPool]") {
    BufferPool pool(5);

    pool.ReleaseBuffer(nullptr);
    REQUIRE(pool.GetPoolSize() == 0);
}

TEST_CASE("BufferPool: SetPoolSize adjusts limit", "[BufferPool]") {
    BufferPool pool(1);

    auto buf1 = pool.AcquireBuffer(64);
    auto buf2 = pool.AcquireBuffer(64);

    pool.ReleaseBuffer(std::move(buf1));
    REQUIRE(pool.GetPoolSize() == 1);

    // Expand pool size
    pool.SetPoolSize(3);
    pool.ReleaseBuffer(std::move(buf2));
    REQUIRE(pool.GetPoolSize() == 2);
}

TEST_CASE("BufferPool: Fresh pool has zero idle buffers", "[BufferPool]") {
    BufferPool pool(10);
    REQUIRE(pool.GetPoolSize() == 0);
}

TEST_CASE("BufferPool: Concurrent acquire and release", "[BufferPool][concurrency]") {
    BufferPool pool(20);
    const int kThreads      = 4;
    const int kOpsPerThread = 50;
    std::atomic<int> acquireCount{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < kOpsPerThread; ++i) {
                auto buf = pool.AcquireBuffer(128);
                if (buf) {
                    acquireCount.fetch_add(1, std::memory_order_relaxed);
                    buf->push_back(static_cast<uint8_t>(i));
                    pool.ReleaseBuffer(std::move(buf));
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    REQUIRE(acquireCount.load() == kThreads * kOpsPerThread);
    // Pool should have at most maxPoolSize buffers
    REQUIRE(pool.GetPoolSize() <= 20);
}
