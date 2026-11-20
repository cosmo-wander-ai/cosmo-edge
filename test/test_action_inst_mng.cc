#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "catch_amalgamated.hpp"
#include "flow/action/ActionInstMngBase.h"
#include "flow/classify/AiClassifyAreaMng.h"
#include "flow/classify/AiClassifyGroupMng.h"
#include "flow/landmark/AiLandmarkMng.h"
#include "flow/ocr/AiOcrMng.h"
#include "flow/target/TargetChooseBestMng.h"
#include "flow/track/AiTracker.h"

using namespace cosmo;

namespace {

ActionNode MakeAction(const std::string& flow = "flow-a") {
    ActionNode action;
    action.flowActionId = flow;
    action.actionId     = "action-a";
    action.actionName   = "Action A";
    action.atomicCode   = "atomic-a";
    return action;
}

template <typename InstPtr>
void CheckIdentity(const InstPtr& inst, const std::string& task, const ActionNode& action) {
    REQUIRE(inst);
    CHECK(inst->GetTaskId() == task);
    CHECK(inst->GetFlowActionId() == action.flowActionId);
    CHECK(inst->GetActionId() == action.actionId);
    CHECK(inst->GetName() == action.actionName);
    CHECK(inst->GetAtomicCode() == action.atomicCode);
}

// Constructors and identity accessors are usable without initializing algorithms or starting threads.
template <typename Manager>
void CheckManagerLifecycle() {
    const std::string task = "task-a/";
    auto action            = MakeAction();
    Manager manager;
    CHECK_FALSE(manager.DeleteInst(nullptr));

    auto original = manager.GetInst(task, action);
    CheckIdentity(original, task, action);
    CHECK(manager.GetInst(task, action) == original);

    auto otherAction = MakeAction("flow-b");
    auto otherFlow   = manager.GetInst(task, otherAction);
    auto otherTask   = manager.GetInst("task-b/", action);
    REQUIRE(otherFlow);
    REQUIRE(otherTask);
    CHECK(otherFlow != original);
    CHECK(otherTask != original);
    CHECK(otherFlow != otherTask);

    auto changed       = action;
    changed.actionId   = "action-changed";
    changed.actionName = "Changed name";
    changed.atomicCode = "atomic-changed";
    changed.configObject.params.emplace_back();
    CHECK(manager.GetInst(task, changed) == original);
    CheckIdentity(original, task, action);

    Manager independent;
    auto independentInst = independent.GetInst(task, action);
    REQUIRE(independentInst);
    CHECK(independentInst != original);

    std::weak_ptr<typename Manager::InstPtr::element_type> oldWeak = original;
    REQUIRE(manager.DeleteInst(original));
    CHECK_FALSE(oldWeak.expired());
    CheckIdentity(original, task, action);
    CHECK(manager.GetInst(task, otherAction) == otherFlow);
    CHECK(manager.GetInst("task-b/", action) == otherTask);
    CHECK(independent.GetInst(task, action) == independentInst);

    auto replacement = manager.GetInst(task, action);
    REQUIRE(replacement);
    CHECK(replacement != original);
    CHECK(manager.GetInst(task, action) == replacement);
    original.reset();
    CHECK(oldWeak.expired());

    typename Manager::InstPtr survivor;
    {
        Manager temporary;
        survivor = temporary.GetInst(task, action);
        REQUIRE(survivor);
    }
    std::weak_ptr<typename Manager::InstPtr::element_type> survivorWeak = survivor;
    CHECK_FALSE(survivorWeak.expired());
    CheckIdentity(survivor, task, action);
    survivor.reset();
    CHECK(survivorWeak.expired());
}

class ConstructionFailure : public std::runtime_error {
public:
    ConstructionFailure() : std::runtime_error("requested construction failure") {}
};

class CountingAction {
public:
    inline static std::atomic<int> attempts{0};
    inline static std::atomic<bool> failNext{false};

    CountingAction(const std::string& task, ActionNode& action) : task_(task), action_(action) {
        ++attempts;
        if (failNext.exchange(false)) {
            throw ConstructionFailure();
        }
    }

    const std::string& GetTaskId() const {
        return task_;
    }

    const std::string& GetFlowActionId() const {
        return action_.flowActionId;
    }

    void QueueStatus(std::vector<AlgActionDataQueueStatus>& statuses, unsigned int durationSec) {
        AlgActionDataQueueStatus status;
        status.actionId = action_.actionId;
        status.taskIds.push_back(task_);
        status.queueStatus.status.periodMs = static_cast<uint64_t>(durationSec) * 1000;
        statuses.push_back(status);
    }

    void ActionInfo(std::vector<ActionRuntimeInfo>& infos) {
        ActionRuntimeInfo info;
        info.actionId = action_.actionId;
        infos.push_back(info);
    }

private:
    std::string task_;
    ActionNode action_;
};

using CountingManager = SimpleMapActionMng<CountingAction>;

std::array<CountingManager::InstPtr, 8> GetConcurrently(CountingManager& manager, const std::string& task,
                                                        const ActionNode& action) {
    std::array<CountingManager::InstPtr, 8> results;
    std::array<std::exception_ptr, 8> errors;
    std::vector<std::thread> threads;
    std::mutex mutex;
    std::condition_variable ready;
    std::condition_variable start;
    size_t waiting = 0;
    bool released  = false;

    auto releaseAndJoin = [&] {
        {
            std::lock_guard<std::mutex> lock(mutex);
            released = true;
        }
        start.notify_all();
        for (auto& thread : threads) {
            thread.join();
        }
    };

    threads.reserve(results.size());
    try {
        for (size_t i = 0; i < results.size(); ++i) {
            threads.emplace_back([&, i, localAction = action]() mutable {
                try {
                    {
                        std::unique_lock<std::mutex> lock(mutex);
                        ++waiting;
                        ready.notify_one();
                        start.wait(lock, [&] { return released; });
                    }
                    results[i] = manager.GetInst(task, localAction);
                } catch (...) {
                    errors[i] = std::current_exception();
                }
            });
        }
    } catch (...) {
        releaseAndJoin();
        throw;
    }
    {
        std::unique_lock<std::mutex> lock(mutex);
        ready.wait(lock, [&] { return waiting == results.size(); });
    }
    releaseAndJoin();
    for (const auto& error : errors) {
        REQUIRE_FALSE(error);
    }
    return results;
}

void CheckReportedAction(CountingManager& manager, const ActionNode& action) {
    std::vector<AlgActionDataQueueStatus> statuses;
    std::vector<ActionRuntimeInfo> infos;
    manager.QueueStatus(statuses, 7);
    manager.ActionInfo(infos);
    REQUIRE(statuses.size() == 1);
    REQUIRE(infos.size() == 1);
    CHECK(statuses.front().actionId == action.actionId);
    CHECK(statuses.front().queueStatus.status.periodMs == 7000);
    CHECK(infos.front().actionId == action.actionId);

    statuses.clear();
    const auto duration = std::numeric_limits<unsigned int>::max();
    manager.QueueStatus(statuses, duration);
    REQUIRE(statuses.size() == 1);
    CHECK(statuses.front().queueStatus.status.periodMs == 4'294'967'295'000ULL);
}

}  // namespace

TEST_CASE("Named action managers preserve identity and ownership", "[ActionInstMng]") {
    SECTION("AiClassifyAreaMng") {
        CheckManagerLifecycle<AiClassifyAreaMng>();
    }
    SECTION("AiClassifyGroupMng") {
        CheckManagerLifecycle<AiClassifyGroupMng>();
    }
    SECTION("AiOcrMng") {
        CheckManagerLifecycle<AiOcrMng>();
    }
    SECTION("TargetChooseBestMng") {
        CheckManagerLifecycle<TargetChooseBestMng>();
    }
    SECTION("AiLandmarkMng") {
        CheckManagerLifecycle<AiLandmarkMng>();
    }
}

TEST_CASE("Tracker name remains distinct from its action name", "[ActionInstMng]") {
    auto action = MakeAction();
    AiTracker tracker("tracking-task", action);
    CHECK(tracker.GetTrackerName() == "tracking-task");
    CHECK(static_cast<const AlgActionBase&>(tracker).GetName() == action.actionName);
}

TEST_CASE("SimpleMapActionMng constructs once for concurrent callers", "[ActionInstMng]") {
    CountingAction::attempts = 0;
    CountingAction::failNext = false;
    CountingManager manager("CountingManager");
    auto action      = MakeAction();
    const auto first = GetConcurrently(manager, "task-a/", action);
    REQUIRE(first.front());
    for (const auto& inst : first) {
        REQUIRE(inst);
        CHECK(inst == first.front());
    }
    CHECK(CountingAction::attempts.load() == 1);

    const auto second = GetConcurrently(manager, "task-a/", action);
    for (const auto& inst : second) {
        REQUIRE(inst);
        CHECK(inst == first.front());
    }
    CHECK(CountingAction::attempts.load() == 1);
}

TEST_CASE("SimpleMapActionMng leaves no entry after construction failure", "[ActionInstMng]") {
    CountingAction::attempts = 0;
    CountingAction::failNext = true;
    CountingManager manager("CountingManager");
    auto action = MakeAction();
    REQUIRE_THROWS_AS(manager.GetInst("task-a/", action), ConstructionFailure);
    REQUIRE(manager.inst_map.empty());
    std::vector<AlgActionDataQueueStatus> statuses;
    std::vector<ActionRuntimeInfo> infos;
    manager.QueueStatus(statuses);
    manager.ActionInfo(infos);
    CHECK(statuses.empty());
    CHECK(infos.empty());

    auto inst = manager.GetInst("task-a/", action);
    REQUIRE(inst);
    CHECK(manager.GetInst("task-a/", action) == inst);
    CHECK(CountingAction::attempts.load() == 2);
    REQUIRE(manager.inst_map.size() == 1);
    CheckReportedAction(manager, action);
}

TEST_CASE("SimpleMapActionMng construction failure preserves other keys", "[ActionInstMng]") {
    CountingAction::attempts = 0;
    CountingAction::failNext = false;
    CountingManager manager("CountingManager");
    auto existingAction = MakeAction("existing-flow");
    auto existing       = manager.GetInst("task-a/", existingAction);
    REQUIRE(existing);

    auto failedAction        = MakeAction("failed-flow");
    failedAction.actionId    = "failed-action";
    CountingAction::failNext = true;
    REQUIRE_THROWS_AS(manager.GetInst("task-a/", failedAction), ConstructionFailure);
    REQUIRE(manager.inst_map.count("task-a/" + failedAction.flowActionId) == 0);
    REQUIRE(manager.inst_map.size() == 1);
    CHECK(manager.GetInst("task-a/", existingAction) == existing);
    CheckReportedAction(manager, existingAction);
    CHECK(manager.DeleteInst(existing));
    CHECK(manager.inst_map.empty());
    CHECK(CountingAction::attempts.load() == 2);
}
