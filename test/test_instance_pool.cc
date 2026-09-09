#include "catch_amalgamated.hpp"
/*
 * test_instance_pool.cc — InstancePool unit tests
 *
 * Tests the InstancePool object lifecycle and pooling behaviors.
 */
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "util/ErrorCode.h"
#include "util/Exec.h"
#include "util/InstancePool.h"

using namespace cosmo;

class MockTaskInstance {
public:
    MockTaskInstance(const std::string& atomic_code, const std::string& cfg_path,
                     const std::string& model_path)
        : atomic_code_(atomic_code), cfg_path_(cfg_path), model_path_(model_path) {}

    util::ErrorEnum Init() {
        if (atomic_code_ == "fail") {
            return util::ErrorEnum::Failed;
        }
        return util::ErrorEnum::Success;
    }

    std::string atomic_code_;
    std::string cfg_path_;
    std::string model_path_;
};

using MockTaskInstancePtr = std::shared_ptr<MockTaskInstance>;

TEST_CASE("InstancePool: Basic Lifecycle", "[instance-pool]") {
    InstancePool<MockTaskInstance, MockTaskInstancePtr> pool("TestPool", 2, 2);

    SECTION("GetInst without prior CreateTask creates an instance") {
        auto inst = pool.GetInst("code", "cfg", "model", 1000);
        REQUIRE(inst != nullptr);
        REQUIRE(inst->atomic_code_ == "code");

        // Return it
        pool.ReturnInst(inst);
    }

    SECTION("Init failure returns nullptr") {
        auto inst = pool.GetInst("fail", "cfg", "model", 100);
        REQUIRE(inst == nullptr);
    }

    SECTION("Pool respects max instance limit") {
        // We configure 2 max_inst_count and 2 inst_per_tasks
        // Let's create enough tasks to allow 2 instances
        pool.CreateTask("code1", "cfg", "model");
        pool.CreateTask("code2", "cfg", "model");
        pool.CreateTask("code3", "cfg", "model");
        pool.CreateTask("code4", "cfg", "model");

        auto inst1 = pool.GetInst("code1", "cfg", "model", 100);
        auto inst2 = pool.GetInst("code2", "cfg", "model", 100);
        REQUIRE(inst1 != nullptr);
        REQUIRE(inst2 != nullptr);
        REQUIRE(inst1 != inst2);

        // Third should fail due to max instance limit and no free instances
        auto inst3 = pool.GetInst("code3", "cfg", "model", 10);
        REQUIRE(inst3 == nullptr);

        pool.ReturnInst(inst1);
        pool.ReturnInst(inst2);
    }
}

namespace {

using namespace std::chrono_literals;
constexpr auto kWaitLimit         = 5s;
constexpr auto kNegativeWaitProbe = 1500ms;

// Used only in the isolated child test, so a deadlocked worker cannot hang the
// parent suite. It remains alive while worker and pool destructors run.
class PoolTestDeadline {
public:
    PoolTestDeadline()
        : watchdog_([this] {
              std::unique_lock<std::mutex> lock(mutex_);
              if (!changed_.wait_for(lock, 20s, [&] { return finished_; })) {
                  std::_Exit(124);
              }
          }) {}

    ~PoolTestDeadline() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            finished_ = true;
            changed_.notify_all();
        }
        watchdog_.join();
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool finished_{false};
    std::thread watchdog_;
};

class PoolGate {
public:
    void Enter() {
        std::unique_lock<std::mutex> lock(mutex_);
        ++entered_;
        changed_.notify_all();
        changed_.wait(lock, [&] { return released_; });
    }

    bool WaitUntilEntered() {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock, kWaitLimit, [&] { return entered_ != 0; });
    }

    void Release() {
        std::lock_guard<std::mutex> lock(mutex_);
        released_ = true;
        changed_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    size_t entered_{0};
    bool released_{false};
};

enum class InitOutcome { kSuccess, kFailure, kException };

struct PoolAttempt {
    InitOutcome outcome{InitOutcome::kSuccess};
    std::shared_ptr<PoolGate> init_gate;
    std::shared_ptr<PoolGate> destroy_gate;
    bool constructor_throws{false};
};

class ControlledPoolInstance;

struct PoolObservations {
    size_t attempts{0};
    size_t live{0};
    size_t peak_live{0};
    size_t initializing{0};
    size_t peak_initializing{0};
    size_t completed_tasks{0};
};

class PoolControl {
public:
    void Add(PoolAttempt attempt) {
        std::lock_guard<std::mutex> lock(mutex_);
        attempts_.push_back(std::move(attempt));
    }

    std::pair<size_t, PoolAttempt> BeginConstruction() {
        std::lock_guard<std::mutex> lock(mutex_);
        auto index = observed_.attempts++;
        instances_.emplace_back();
        return {index, index < attempts_.size() ? attempts_[index] : PoolAttempt{}};
    }

    void Constructed() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++observed_.live;
        observed_.peak_live = std::max(observed_.peak_live, observed_.live);
    }

    void BeginInit(size_t index, std::weak_ptr<ControlledPoolInstance> inst) {
        std::lock_guard<std::mutex> lock(mutex_);
        instances_[index] = std::move(inst);
        ++observed_.initializing;
        observed_.peak_initializing = std::max(observed_.peak_initializing, observed_.initializing);
        changed_.notify_all();
    }

    void EndInit() {
        std::lock_guard<std::mutex> lock(mutex_);
        --observed_.initializing;
        changed_.notify_all();
    }

    void Destroyed() {
        std::lock_guard<std::mutex> lock(mutex_);
        --observed_.live;
    }

    void TaskCompleted() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++observed_.completed_tasks;
        changed_.notify_all();
    }

    bool WaitUntilTasksAccountedFor(size_t count) {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock, kWaitLimit,
                                 [&] { return observed_.initializing + observed_.completed_tasks == count; });
    }

    PoolObservations Observe() {
        std::lock_guard<std::mutex> lock(mutex_);
        return observed_;
    }

    std::weak_ptr<ControlledPoolInstance> Instance(size_t index) {
        std::lock_guard<std::mutex> lock(mutex_);
        return instances_.at(index);
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<PoolAttempt> attempts_;
    std::vector<std::weak_ptr<ControlledPoolInstance>> instances_;
    PoolObservations observed_;
};

class ControlledPoolInstance : public std::enable_shared_from_this<ControlledPoolInstance> {
public:
    inline static std::shared_ptr<PoolControl> current_control;

    ControlledPoolInstance(const std::string&, const std::string&, const std::string&)
        : control_(current_control) {
        auto construction = control_->BeginConstruction();
        index_            = construction.first;
        attempt_          = std::move(construction.second);
        if (attempt_.constructor_throws) {
            throw std::runtime_error("controlled constructor failure");
        }
        control_->Constructed();
    }

    ~ControlledPoolInstance() {
        if (attempt_.destroy_gate) {
            attempt_.destroy_gate->Enter();
        }
        control_->Destroyed();
    }

    util::ErrorEnum Init() {
        control_->BeginInit(index_, weak_from_this());
        if (attempt_.init_gate) {
            attempt_.init_gate->Enter();
        }
        control_->EndInit();
        if (attempt_.outcome == InitOutcome::kException) {
            throw std::runtime_error("controlled init failure");
        }
        return attempt_.outcome == InitOutcome::kFailure ? util::ErrorEnum::Failed : util::ErrorEnum::Success;
    }

private:
    std::shared_ptr<PoolControl> control_;
    size_t index_{0};
    PoolAttempt attempt_;
};

using ControlledInstancePtr = std::shared_ptr<ControlledPoolInstance>;
using ControlledPool        = InstancePool<ControlledPoolInstance, ControlledInstancePtr>;

class ScopedPoolControl {
public:
    explicit ScopedPoolControl(std::shared_ptr<PoolControl> state)
        : previous_(std::exchange(ControlledPoolInstance::current_control, std::move(state))) {}
    ~ScopedPoolControl() {
        ControlledPoolInstance::current_control = std::move(previous_);
    }

private:
    std::shared_ptr<PoolControl> previous_;
};

struct PoolFixture {
    std::shared_ptr<PoolControl> control{std::make_shared<PoolControl>()};
    ScopedPoolControl scope{control};
    ControlledPool pool;

    explicit PoolFixture(size_t per_tasks = 1, size_t maximum = 1)
        : pool("ControlledPool", per_tasks, maximum) {}
};

// Futures from packaged_task do not block during assertion unwinding. The owner
// releases every gate before joining, and must be destroyed before the pool.
class PoolWorkers {
public:
    explicit PoolWorkers(std::vector<std::shared_ptr<PoolGate>> gates = {}) : gates_(std::move(gates)) {}

    ~PoolWorkers() {
        Release();
        Join();
    }

    template <typename Fn>
    auto Start(Fn fn) {
        std::packaged_task<decltype(fn())()> task(std::move(fn));
        auto result = task.get_future();
        threads_.emplace_back(std::move(task));
        return result;
    }

    void Release() {
        for (const auto& gate : gates_) {
            gate->Release();
        }
    }

    void Join() {
        for (auto& thread : threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

private:
    std::vector<std::shared_ptr<PoolGate>> gates_;
    std::vector<std::thread> threads_;
};

}  // namespace

TEST_CASE("InstancePool: recovery preserves one registration", "[instance-pool][lifecycle]") {
    PoolFixture fx;
    fx.control->Add({InitOutcome::kFailure});
    fx.pool.CreateTask("code", "cfg", "model");
    auto inst = fx.pool.GetInst("code", "cfg", "model");
    REQUIRE(inst);
    std::weak_ptr<ControlledPoolInstance> weak = inst;
    fx.pool.ReturnInst(inst);
    inst.reset();
    CHECK_FALSE(weak.expired());
    fx.pool.DeleteTask();
    CHECK(weak.expired());
}

TEST_CASE("InstancePool: repeated failures leave recovery capacity", "[instance-pool][lifecycle]") {
    PoolFixture fx;
    for (int i = 0; i < 4; ++i) {
        fx.control->Add({InitOutcome::kFailure});
    }
    fx.pool.CreateTask("code", "cfg", "model");
    for (int i = 0; i < 3; ++i) {
        CHECK_FALSE(fx.pool.GetInst("code", "cfg", "model"));
        CHECK(fx.control->Observe().live == 0);
    }
    auto inst = fx.pool.GetInst("code", "cfg", "model");
    REQUIRE(inst);
    CHECK(fx.control->Observe().attempts == 5);
    fx.pool.ReturnInst(inst);
    inst.reset();
    CHECK(fx.control->Observe().live == 1);
    auto recovered = fx.pool.GetInst("code", "cfg", "model");
    REQUIRE(recovered);
    CHECK(fx.control->Observe().attempts == 5);
    fx.pool.ReturnInst(recovered);
    recovered.reset();
    fx.pool.DeleteTask();
    CHECK(fx.control->Observe().live == 0);
}

TEST_CASE("InstancePool: unregistered acquisitions retire each generation", "[instance-pool][lifecycle]") {
    PoolFixture fx;
    for (size_t generation = 0; generation < 2; ++generation) {
        auto inst = fx.pool.GetInst("code", "cfg", "model");
        REQUIRE(inst);
        std::weak_ptr<ControlledPoolInstance> weak = inst;
        CHECK(fx.control->Observe().attempts == generation + 1);
        fx.pool.ReturnInst(inst);
        inst.reset();
        CHECK(weak.expired());
    }
}

TEST_CASE("InstancePool: borrowed instances outlive the final registration", "[instance-pool][lifecycle]") {
    PoolFixture fx(1, 3);
    std::vector<ControlledInstancePtr> borrowed;
    for (int i = 0; i < 3; ++i) {
        fx.pool.CreateTask("code", "cfg", "model");
        borrowed.push_back(fx.pool.GetInst("code", "cfg", "model"));
        REQUIRE(borrowed.back());
    }
    for (int i = 0; i < 3; ++i) {
        fx.pool.DeleteTask();
    }
    CHECK(fx.control->Observe().live == 3);
    CHECK_FALSE(fx.pool.GetInst("code", "cfg", "model", 0));
    CHECK(fx.control->Observe().attempts == 3);
    for (auto& inst : borrowed) {
        std::weak_ptr<ControlledPoolInstance> weak = inst;
        fx.pool.ReturnInst(inst);
        inst.reset();
        CHECK(weak.expired());
    }
}

TEST_CASE("InstancePool: late prewarm cannot revive a deleted registration", "[instance-pool][concurrency]") {
    PoolFixture fx;
    auto gate = std::make_shared<PoolGate>();
    fx.control->Add({InitOutcome::kSuccess, gate});
    PoolWorkers workers({gate});
    auto creating                  = workers.Start([&] { fx.pool.CreateTask("code", "cfg", "model"); });
    const bool entered             = gate->WaitUntilEntered();
    auto deleting                  = workers.Start([&] { fx.pool.DeleteTask(); });
    const bool deleted_during_init = deleting.wait_for(kWaitLimit) == std::future_status::ready;
    workers.Release();
    workers.Join();
    REQUIRE(entered);
    CHECK(deleted_during_init);
    REQUIRE_NOTHROW(creating.get());
    REQUIRE_NOTHROW(deleting.get());
    CHECK(fx.control->Instance(0).expired());
}

TEST_CASE("InstancePool: an acquisition can finish after its registration ends",
          "[instance-pool][concurrency]") {
    PoolFixture fx;
    auto gate = std::make_shared<PoolGate>();
    fx.control->Add({InitOutcome::kFailure});
    fx.control->Add({InitOutcome::kSuccess, gate});
    fx.pool.CreateTask("code", "cfg", "model");
    PoolWorkers workers({gate});
    auto acquiring                 = workers.Start([&] { return fx.pool.GetInst("code", "cfg", "model"); });
    const bool entered             = gate->WaitUntilEntered();
    auto deleting                  = workers.Start([&] { fx.pool.DeleteTask(); });
    const bool deleted_during_init = deleting.wait_for(kWaitLimit) == std::future_status::ready;
    workers.Release();
    workers.Join();
    REQUIRE(entered);
    CHECK(deleted_during_init);
    REQUIRE_NOTHROW(deleting.get());
    auto inst = acquiring.get();
    REQUIRE(inst);
    auto weak = fx.control->Instance(1);
    CHECK_FALSE(weak.expired());
    fx.pool.ReturnInst(inst);
    inst.reset();
    CHECK(weak.expired());
}

TEST_CASE("InstancePool: scaling follows registration thresholds", "[instance-pool][lifecycle]") {
    PoolFixture fx(2, 2);
    for (size_t tasks = 1; tasks <= 6; ++tasks) {
        fx.pool.CreateTask("code", "cfg", "model");
        CHECK(fx.control->Observe().attempts == std::min<size_t>(2, (tasks + 1) / 2));
    }
    auto first  = fx.pool.GetInst("code", "cfg", "model");
    auto second = fx.pool.GetInst("code", "cfg", "model");
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first != second);
    CHECK_FALSE(fx.pool.GetInst("code", "cfg", "model", 0));
    fx.pool.ReturnInst(first);
    fx.pool.ReturnInst(second);
    first.reset();
    second.reset();
    for (int i = 0; i < 6; ++i) {
        fx.pool.DeleteTask();
    }
    CHECK(fx.control->Observe().live == 0);
}

TEST_CASE("InstancePool: partial demand retires surplus returns", "[instance-pool][lifecycle]") {
    PoolFixture fx(1, 3);
    std::vector<ControlledInstancePtr> borrowed;
    for (int i = 0; i < 3; ++i) {
        fx.pool.CreateTask("code", "cfg", "model");
        borrowed.push_back(fx.pool.GetInst("code", "cfg", "model"));
        REQUIRE(borrowed.back());
    }
    fx.pool.DeleteTask();
    fx.pool.DeleteTask();
    for (size_t i = 0; i < borrowed.size(); ++i) {
        std::weak_ptr<ControlledPoolInstance> weak = borrowed[i];
        fx.pool.ReturnInst(borrowed[i]);
        borrowed[i].reset();
        CHECK(weak.expired() == (i < 2));
    }
    auto retained = fx.pool.GetInst("code", "cfg", "model");
    REQUIRE(retained);
    CHECK(fx.control->Observe().attempts == 3);
    fx.pool.ReturnInst(retained);
    retained.reset();
    fx.pool.DeleteTask();
    CHECK(fx.control->Observe().live == 0);
}

TEST_CASE("InstancePool: concurrent prewarms reserve capacity before construction",
          "[instance-pool][concurrency]") {
    constexpr size_t kCalls = 6;
    PoolFixture fx(1, 2);
    auto gate = std::make_shared<PoolGate>();
    for (size_t i = 0; i < kCalls; ++i) {
        fx.control->Add({InitOutcome::kSuccess, gate});
    }
    PoolWorkers workers({gate});
    std::vector<std::future<void>> calls;
    for (size_t i = 0; i < kCalls; ++i) {
        calls.push_back(workers.Start([&] {
            fx.pool.CreateTask("code", "cfg", "model");
            fx.control->TaskCompleted();
        }));
    }
    const bool accounted = fx.control->WaitUntilTasksAccountedFor(kCalls);
    const auto peak      = fx.control->Observe();
    workers.Release();
    workers.Join();
    REQUIRE(accounted);
    for (auto& call : calls) {
        REQUIRE_NOTHROW(call.get());
    }
    CHECK(peak.peak_initializing <= 2);
    CHECK(peak.peak_live <= 2);
    for (size_t i = 0; i < kCalls; ++i) {
        fx.pool.DeleteTask();
    }
    CHECK(fx.control->Observe().live == 0);
}

TEST_CASE("InstancePool: creation exceptions propagate without consuming capacity",
          "[instance-pool][lifecycle]") {
    const bool prewarm            = GENERATE(false, true);
    const bool constructor_throws = GENERATE(false, true);
    PoolFixture fx;
    PoolAttempt attempt;
    attempt.constructor_throws = constructor_throws;
    attempt.outcome            = InitOutcome::kException;
    fx.control->Add(attempt);
    std::string message;
    try {
        if (prewarm) {
            fx.pool.CreateTask("code", "cfg", "model");
        } else {
            fx.pool.GetInst("code", "cfg", "model");
        }
    } catch (const std::runtime_error& error) {
        message = error.what();
    }
    REQUIRE(message == (constructor_throws ? "controlled constructor failure" : "controlled init failure"));
    CHECK(fx.control->Observe().live == 0);
    auto inst = fx.pool.GetInst("code", "cfg", "model");
    REQUIRE(inst);
    fx.pool.ReturnInst(inst);
    inst.reset();
    if (prewarm) {
        CHECK(fx.control->Observe().live == 1);
        fx.pool.DeleteTask();
    }
    CHECK(fx.control->Observe().live == 0);
}

TEST_CASE("InstancePool: invalid returns cannot add available instances", "[instance-pool][lifecycle]") {
    PoolFixture fx;
    fx.pool.CreateTask("code", "cfg", "model");
    auto inst = fx.pool.GetInst("code", "cfg", "model");
    REQUIRE(inst);
    fx.pool.ReturnInst(nullptr);
    fx.pool.ReturnInst(inst);
    fx.pool.ReturnInst(inst);
    auto unknown = std::make_shared<ControlledPoolInstance>("other", "cfg", "model");
    std::weak_ptr<ControlledPoolInstance> unknown_weak = unknown;
    fx.pool.ReturnInst(unknown);
    unknown.reset();
    CHECK(unknown_weak.expired());
    auto again = fx.pool.GetInst("code", "cfg", "model");
    CHECK(again == inst);
    CHECK_FALSE(fx.pool.GetInst("code", "cfg", "model", 0));
    fx.pool.ReturnInst(again);
    again.reset();
    inst.reset();
    fx.pool.DeleteTask();
    CHECK(fx.control->Observe().live == 0);
}

TEST_CASE("InstancePool: waiting preserves timeout and return behavior", "[instance-pool][concurrency]") {
    const int timeout_ms = GENERATE(0, 10, 35);
    PoolFixture fx;
    fx.pool.CreateTask("code", "cfg", "model");
    auto held = fx.pool.GetInst("code", "cfg", "model");
    REQUIRE(held);
    CHECK_FALSE(fx.pool.GetInst("code", "cfg", "model", timeout_ms));
    CHECK(fx.control->Observe().attempts == 1);
    fx.pool.ReturnInst(held);
    held.reset();
    fx.pool.DeleteTask();
    CHECK(fx.control->Observe().live == 0);
}

TEST_CASE("InstancePool: negative timeout waits for a return", "[instance-pool][concurrency]") {
    // Resolve in the parent, before exec replaces /proc/self/exe.
    const auto binary = std::filesystem::canonical("/proc/self/exe").string();
    std::string output;
    const auto result = util::Exec({binary, "[.instance-pool-negative-child]"}, output);
    INFO(output);
    REQUIRE(result == 0);
}

TEST_CASE("InstancePool: negative timeout child", "[.instance-pool-negative-child]") {
    PoolTestDeadline deadline;
    PoolFixture fx;
    fx.pool.CreateTask("code", "cfg", "model");
    auto held = fx.pool.GetInst("code", "cfg", "model");
    REQUIRE(held);
    std::promise<void> started;
    auto start = started.get_future();
    PoolWorkers workers;
    auto waiting = workers.Start([&] {
        started.set_value();
        return fx.pool.GetInst("code", "cfg", "model", -1);
    });
    REQUIRE(start.wait_for(kWaitLimit) == std::future_status::ready);
    // The negative wait must outlast the default positive polling budget.
    const bool still_waiting = waiting.wait_for(kNegativeWaitProbe) == std::future_status::timeout;
    fx.pool.ReturnInst(held);
    auto weak = std::weak_ptr<ControlledPoolInstance>(held);
    held.reset();
    const bool finished = waiting.wait_for(kWaitLimit) == std::future_status::ready;
    workers.Join();
    CHECK(still_waiting);
    REQUIRE(finished);
    auto acquired = waiting.get();
    REQUIRE(acquired);
    CHECK(acquired == weak.lock());
    fx.pool.ReturnInst(acquired);
    acquired.reset();
    fx.pool.DeleteTask();
    CHECK(weak.expired());
}

TEST_CASE("InstancePool: retiring an idle model does not hold the pool lock",
          "[instance-pool][concurrency]") {
    PoolFixture fx;
    auto gate = std::make_shared<PoolGate>();
    fx.control->Add({InitOutcome::kSuccess, nullptr, gate});
    fx.pool.CreateTask("code", "cfg", "model");
    PoolWorkers workers({gate});
    auto retiring         = workers.Start([&] { fx.pool.DeleteTask(); });
    const bool entered    = gate->WaitUntilEntered();
    auto other_operation  = workers.Start([&] { fx.pool.DeleteTask(); });
    const bool progressed = other_operation.wait_for(kWaitLimit) == std::future_status::ready;
    workers.Release();
    workers.Join();
    REQUIRE(entered);
    CHECK(progressed);
    REQUIRE_NOTHROW(retiring.get());
    REQUIRE_NOTHROW(other_operation.get());
    CHECK(fx.control->Instance(0).expired());
}

TEST_CASE("InstancePool: successful prewarm wins demand over pending initialization",
          "[instance-pool][concurrency]") {
    PoolFixture fx(1, 2);
    auto first_gate  = std::make_shared<PoolGate>();
    auto second_gate = std::make_shared<PoolGate>();
    fx.control->Add({InitOutcome::kSuccess, first_gate});
    fx.control->Add({InitOutcome::kFailure, second_gate});
    PoolWorkers workers({first_gate, second_gate});
    auto first = workers.Start([&] { fx.pool.CreateTask("code", "cfg", "model"); });
    REQUIRE(first_gate->WaitUntilEntered());
    auto second = workers.Start([&] { fx.pool.CreateTask("code", "cfg", "model"); });
    REQUIRE(second_gate->WaitUntilEntered());
    fx.pool.DeleteTask();
    first_gate->Release();
    REQUIRE(first.wait_for(kWaitLimit) == std::future_status::ready);
    REQUIRE_NOTHROW(first.get());
    auto inst = fx.pool.GetInst("code", "cfg", "model", 0);
    workers.Release();
    workers.Join();
    REQUIRE_NOTHROW(second.get());
    REQUIRE(inst);
    CHECK(inst == fx.control->Instance(0).lock());
    fx.pool.ReturnInst(inst);
    inst.reset();
    fx.pool.DeleteTask();
    CHECK(fx.control->Observe().live == 0);
}

TEST_CASE("InstancePool: late acquisition trims surplus prewarmed instances",
          "[instance-pool][concurrency]") {
    PoolFixture fx(1, 3);
    auto acquire_gate = std::make_shared<PoolGate>();
    auto prewarm_gate = std::make_shared<PoolGate>();
    fx.control->Add({InitOutcome::kSuccess, acquire_gate});
    fx.control->Add({InitOutcome::kSuccess, prewarm_gate});
    PoolWorkers workers({acquire_gate, prewarm_gate});
    auto acquiring = workers.Start([&] { return fx.pool.GetInst("code", "cfg", "model"); });
    REQUIRE(acquire_gate->WaitUntilEntered());
    auto first  = workers.Start([&] { fx.pool.CreateTask("code", "cfg", "model"); });
    auto second = workers.Start([&] { fx.pool.CreateTask("code", "cfg", "model"); });
    REQUIRE(prewarm_gate->WaitUntilEntered());
    prewarm_gate->Release();
    REQUIRE(first.wait_for(kWaitLimit) == std::future_status::ready);
    REQUIRE(second.wait_for(kWaitLimit) == std::future_status::ready);
    REQUIRE_NOTHROW(first.get());
    REQUIRE_NOTHROW(second.get());
    fx.pool.DeleteTask();
    workers.Release();
    workers.Join();
    auto inst = acquiring.get();
    REQUIRE(inst);
    CHECK(fx.control->Instance(1).expired());
    fx.pool.ReturnInst(inst);
    inst.reset();
    fx.pool.DeleteTask();
    CHECK(fx.control->Observe().live == 0);
}

TEST_CASE("InstancePool: zero limits and excess deletes remain defined", "[instance-pool][lifecycle]") {
    SECTION("A zero capacity never starts construction") {
        PoolFixture fx(1, 0);
        fx.pool.CreateTask("code", "cfg", "model");
        CHECK_FALSE(fx.pool.GetInst("code", "cfg", "model", 0));
        CHECK(fx.control->Observe().attempts == 0);
        fx.pool.DeleteTask();
        fx.pool.DeleteTask();
    }
    SECTION("A zero threshold admits one prewarm per registration") {
        PoolFixture fx(0, 2);
        fx.pool.CreateTask("code", "cfg", "model");
        CHECK(fx.control->Observe().attempts == 1);
        fx.pool.CreateTask("code", "cfg", "model");
        CHECK(fx.control->Observe().attempts == 2);
        fx.pool.DeleteTask();
        CHECK(fx.control->Observe().live == 2);
        fx.pool.DeleteTask();
        CHECK(fx.control->Observe().live == 0);
    }
    SECTION("Excess deletes cannot offset the next registration") {
        PoolFixture fx;
        fx.pool.DeleteTask();
        fx.pool.DeleteTask();
        fx.pool.CreateTask("code", "cfg", "model");
        auto inst = fx.pool.GetInst("code", "cfg", "model");
        REQUIRE(inst);
        fx.pool.ReturnInst(inst);
        inst.reset();
        fx.pool.DeleteTask();
        CHECK(fx.control->Observe().live == 0);
    }
}
