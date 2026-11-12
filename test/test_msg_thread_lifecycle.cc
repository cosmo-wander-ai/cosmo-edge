#include <atomic>
#include <thread>
#include <vector>

#include "catch_amalgamated.hpp"
#include "network/msg/MsgThread.h"

namespace {

class CountingMsgThread final : public cosmo::MsgThread {
public:
    CountingMsgThread() : MsgThread("counting-test", 1024) {}

    std::atomic<int> handled{0};

private:
    void HandleMsg(cosmo::MsgEnvelope& /*msg*/) override {
        handled.fetch_add(1, std::memory_order_relaxed);
    }
};

class ReentrantClearMsgThread final : public cosmo::MsgThread {
public:
    ReentrantClearMsgThread() : MsgThread("reentrant-clear-test", 1) {}

    void ArmReentry() {
        reenter_on_clear_.store(true, std::memory_order_release);
    }

    std::atomic<int> cleared{0};

private:
    void HandleMsg(cosmo::MsgEnvelope& /*msg*/) override {}

    void ClearMsg(cosmo::MsgEnvelope& /*msg*/) override {
        cleared.fetch_add(1, std::memory_order_relaxed);
        if (reenter_on_clear_.exchange(false, std::memory_order_acq_rel)) {
            Put(cosmo::MsgEnvelope(99, nullptr));
        }
    }

    std::atomic<bool> reenter_on_clear_{false};
};

}  // namespace

TEST_CASE("MsgThread drain rejects late producers and drains accepted work",
          "[network][msg-thread][concurrency]") {
    CountingMsgThread worker;
    REQUIRE(worker.start());

    constexpr int kMessageCount = 100;
    for (int i = 0; i < kMessageCount; ++i) {
        worker.Put(cosmo::MsgEnvelope(i + 1, nullptr));
    }

    worker.DrainAndStop();

    REQUIRE(worker.handled.load(std::memory_order_relaxed) == kMessageCount);
    REQUIRE(worker.MsgSize() == 0);
    worker.Put(cosmo::MsgEnvelope(1000, nullptr));
    REQUIRE(worker.MsgSize() == 0);
}

TEST_CASE("MsgThread concurrent stop is idempotent", "[network][msg-thread][concurrency]") {
    CountingMsgThread worker;
    REQUIRE(worker.start());

    std::vector<std::thread> stoppers;
    for (int i = 0; i < 8; ++i) {
        stoppers.emplace_back([&worker]() { worker.Stop(); });
    }
    for (auto& thread : stoppers) {
        thread.join();
    }

    REQUIRE(worker.MsgSize() == 0);
}

TEST_CASE("MsgThread may restart after a completed stop", "[network][msg-thread][lifecycle]") {
    CountingMsgThread worker;
    cosmo::util::Thread& thread = worker;
    REQUIRE(thread.start());
    worker.Stop();

    REQUIRE(thread.start());
    REQUIRE(worker.Put(cosmo::MsgEnvelope(1, nullptr)) == 1);
    worker.DrainAndStop();

    REQUIRE(worker.handled.load(std::memory_order_relaxed) == 1);
    REQUIRE(worker.MsgSize() == 0);
}

TEST_CASE("MsgThread full queue cleanup may re-enter Put", "[network][msg-thread][concurrency]") {
    ReentrantClearMsgThread worker;
    REQUIRE(worker.Put(cosmo::MsgEnvelope(1, nullptr)) == 1);

    worker.ArmReentry();
    REQUIRE(worker.Put(cosmo::MsgEnvelope(2, nullptr)) == -1);

    // The full-queue rejection clears message 2. Its callback re-enters Put,
    // and that rejection clears message 99 without deadlocking on mtx_.
    REQUIRE(worker.cleared.load(std::memory_order_relaxed) == 2);
    REQUIRE(worker.MsgSize() == 1);
    worker.Stop();
}

TEST_CASE("MsgThread stopped rejection cleanup may re-enter Put", "[network][msg-thread][concurrency]") {
    ReentrantClearMsgThread worker;
    worker.Stop();
    const int cleared_before = worker.cleared.load(std::memory_order_relaxed);

    worker.ArmReentry();
    REQUIRE(worker.Put(cosmo::MsgEnvelope(1, nullptr)) == -1);

    REQUIRE(worker.cleared.load(std::memory_order_relaxed) == cleared_before + 2);
    REQUIRE(worker.MsgSize() == 0);
}
