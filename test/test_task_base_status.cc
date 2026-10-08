#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "catch_amalgamated.hpp"
#include "flow/task/TaskBase.h"
#include "mock/MockAppInfoService.h"
#include "service/ai/impl/LlmInferServiceImpl.h"
#include "support/ScopedServiceOverride.h"
#include "util/dto/ActionCodes.h"

namespace cosmo {
namespace {

    // Public status order, independent of the order in which a workflow creates its actions.
    constexpr std::array<std::string_view, 23> kActionStatusOrder = {
        AADetect_Code,
        DADinoDetect_Code,
        DAQwen3VL_Code,
        DASam2Segment_Code,
        AATrack_Code,
        AAClassify_Code,
        AAClassifyGroup_Code,
        AAClassifyArea_Code,
        AAClassifyAttr_Code,
        AALandmark_Code,
        AAOcr_Code,
        AARecognizer_Code,
        AAVideoDiagnosis_Code,
        BAFilter_Code,
        BALogicalJudgment_Code,
        BASensitivity_Code,
        BAPositiveSaveSensitivity_Code,
        BATaskAlarm_Code,
        BAAreaAlarm_Code,
        BAFaceLogic_Code,
        BATaskFaceAlarm_Code,
        BAActionBranch_Code,
        BATargetChooseBest_Code,
    };

    ActionAlgPtr MakeStatusAlgorithm(const std::vector<std::string_view>& action_ids) {
        auto algorithm           = std::make_shared<ActionAlg>();
        algorithm->algorithmCode = "status-algorithm";
        algorithm->algorithmName = "Status algorithm";
        for (auto action_id : action_ids) {
            ActionNode action;
            action.actionId     = action_id;
            action.flowActionId = "flow-" + action.actionId;
            action.atomicCode   = "status-" + action.actionId;
            action.atomAlgName  = action.atomicCode;
            action.actionName   = action.atomicCode;
            algorithm->workFlow.push_back(action);
        }
        return algorithm;
    }

    AlgActionBasePtr FindAction(const TaskElementPtr& task, std::string_view action_id) {
        auto it = std::find_if(task->actions.begin(), task->actions.end(),
                               [&](const auto& action) { return action.action.actionId == action_id; });
        REQUIRE(it != task->actions.end());
        REQUIRE(it->actionInst);
        return it->actionInst;
    }

    AlgTaskUnit MakeBinding(const TaskElementPtr& task, std::string_view action_id) {
        auto action = FindAction(task, action_id);
        AlgTaskUnit binding;
        binding.channel_id   = task->channelId;
        binding.task_id      = task->taskId;
        binding.actionId     = action->GetActionId();
        binding.flowActionId = action->GetFlowActionId();
        binding.fps          = 5.0f;
        binding.que          = action->GetQueue();
        return binding;
    }

    void CheckSon(const ActionRuntimeSon& son, const AlgTaskUnit& binding) {
        CHECK(son.channelId == binding.channel_id);
        CHECK(son.taskId == binding.task_id);
        CHECK(son.actionId == binding.actionId);
    }

}  // namespace

TEST_CASE("TaskBase empty aggregation preserves caller entries", "[TaskBase][status]") {
    TaskBase task_base;
    std::vector<AlgActionDataQueueStatus> statuses;
    std::vector<ActionRuntimeInfo> infos;
    task_base.QueueStatus(statuses);
    task_base.ActionInfo(infos);
    CHECK(statuses.empty());
    CHECK(infos.empty());

    statuses.emplace_back().actionId = "existing-status";
    infos.emplace_back().actionId    = "existing-info";
    task_base.QueueStatus(statuses, 7);
    task_base.ActionInfo(infos);
    REQUIRE(statuses.size() == 1);
    REQUIRE(infos.size() == 1);
    CHECK(statuses.front().actionId == "existing-status");
    CHECK(infos.front().actionId == "existing-info");
}

TEST_CASE("TaskBase aggregates every manager in status order with the requested window",
          "[TaskBase][status]") {
    test::MockAppInfoService app_info;
    test::ScopedServiceOverride<service::IAppInfoService> app_info_guard(app_info);
    REQUIRE_CALL(app_info, GetNumber()).RETURN(0);
    service::LlmInferServiceImpl llm_infer;
    test::ScopedServiceOverride<service::ILlmInferService> llm_infer_guard(llm_infer);
    TaskBase task_base;
    const std::vector<std::string_view> creation_order(kActionStatusOrder.rbegin(),
                                                       kActionStatusOrder.rend());
    auto task = task_base.TaskCreate("status-channel", "Status channel", "status-task",
                                     MakeStatusAlgorithm(creation_order));
    REQUIRE(task);
    REQUIRE(task->actions.size() == kActionStatusOrder.size() + 1);

    // Register real downstream queues without starting workers or opening a stream.
    auto detection = MakeBinding(task, AADetect_Code);
    REQUIRE(task_base.GetChannelInst(task->channelId)->RegistTaskQueue(detection));
    auto branch = MakeBinding(task, BAActionBranch_Code);
    REQUIRE(FindAction(task, BAFilter_Code)->RegistTaskQueue(branch));

    std::vector<std::weak_ptr<AlgActionBase>> instances;
    for (const auto& action : task->actions) {
        REQUIRE(action.actionInst);
        instances.push_back(action.actionInst);
        if (action.action.actionId == BAStreamChannel_Code) {
            continue;
        }
        const auto queue = action.actionInst->GetQueue();
        const auto frame = std::make_shared<AlgData>();
        REQUIRE(queue->Insert(frame));
        REQUIRE(queue->Pop() == frame);
        REQUIRE(queue->Insert(frame));
    }

    // Statistics report completed one-second buckets; wait for the seeded bucket to close.
    const auto next_second =
        std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::steady_clock::now()) +
        std::chrono::seconds(1);
    std::this_thread::sleep_until(next_second);

    std::vector<AlgActionDataQueueStatus> statuses(1);
    statuses.front().actionId = "existing-status";
    task_base.QueueStatus(statuses);
    std::vector<AlgActionDataQueueStatus> zero_window;
    task_base.QueueStatus(zero_window, 0);
    std::vector<ActionRuntimeInfo> infos(1);
    infos.front().actionId = "existing-info";
    task_base.ActionInfo(infos);

    REQUIRE(statuses.size() == kActionStatusOrder.size() + 3);
    REQUIRE(zero_window.size() == statuses.size() - 1);
    REQUIRE(infos.size() == statuses.size());
    CHECK(statuses.front().actionId == "existing-status");
    CHECK(infos.front().actionId == "existing-info");
    CHECK(statuses[1].actionId == std::string(BAStreamChannel_Code) + " DEMUX");
    CHECK(statuses[2].actionId == std::string(BAStreamChannel_Code) + " DECODE");
    CHECK(statuses[1].actionStatus == util::ErrorEnum::ActionReady);
    CHECK(statuses[2].actionStatus == util::ErrorEnum::NotInit);
    CHECK(statuses[2].channelIds == std::vector<std::string>{task->channelId});
    CHECK(infos[1].actionId == statuses[1].actionId);
    CHECK(infos[1].sons.empty());
    CHECK(infos[2].actionId == statuses[2].actionId);
    CHECK(infos[2].maxTaskFps == detection.fps);
    CHECK(infos[2].queueName == detection.que->Name());
    REQUIRE(infos[2].sons.size() == 1);
    CheckSon(infos[2].sons.front(), detection);
    CHECK(infos[2].sons.front().fps == detection.fps);
    CHECK(infos[2].sons.front().queueName == detection.que->Name());

    for (size_t i = 0; i < kActionStatusOrder.size(); ++i) {
        const auto action_id = kActionStatusOrder[i];
        CAPTURE(action_id);
        const auto& status      = statuses[i + 3];
        const auto& zero_status = zero_window[i + 2];
        const auto& info        = infos[i + 3];
        const auto queue        = FindAction(task, action_id)->GetQueue();
        CHECK(status.actionId == action_id);
        CHECK(zero_status.actionId == action_id);
        CHECK(info.actionId == action_id);
        CHECK(status.actionStatus == util::ErrorEnum::ActionReady);
        CHECK(status.taskIds == std::vector<std::string>{task->taskId});
        CHECK(status.queueStatus.name == queue->Name());
        CHECK(status.queueStatus.queSize == queue->GetMaxSize());
        CHECK(status.queueStatus.queLength == 1);
        CHECK(status.queueStatus.status.insertCount == 2);
        CHECK(status.queueStatus.status.processCount == 1);
        CHECK(status.queueStatus.status.insertCountPeriod == 2);
        CHECK(status.queueStatus.status.processCountPeriod == 1);
        CHECK(zero_status.queueStatus.status.insertCount == 2);
        CHECK(zero_status.queueStatus.status.processCount == 1);
        CHECK(zero_status.queueStatus.status.insertCountPeriod == 0);
        CHECK(zero_status.queueStatus.status.processCountPeriod == 0);
        if (i < 4) {
            CHECK(status.channelIds == std::vector<std::string>{task->channelId});
        }
        if (action_id == BAFilter_Code) {
            REQUIRE(info.sons.size() == 1);
            CheckSon(info.sons.front(), branch);
        } else {
            CHECK(info.sons.empty());
        }
    }

    REQUIRE(task_base.TaskDelete(task));
    for (const auto& instance : instances) {
        CHECK(instance.expired());
    }
    statuses.clear();
    infos.clear();
    task_base.QueueStatus(statuses);
    task_base.ActionInfo(infos);
    CHECK(statuses.empty());
    CHECK(infos.empty());
}

TEST_CASE("TaskBase aggregation retains shared instances until the last task is deleted",
          "[TaskBase][status][lifecycle]") {
    test::MockAppInfoService app_info;
    test::ScopedServiceOverride<service::IAppInfoService> app_info_guard(app_info);
    REQUIRE_CALL(app_info, GetNumber()).RETURN(0);
    TaskBase task_base;
    auto algorithm = MakeStatusAlgorithm({BAFilter_Code, AAClassify_Code, AADetect_Code});
    auto first     = task_base.TaskCreate("shared-channel", "Shared channel", "task-z", algorithm);
    auto second    = task_base.TaskCreate("shared-channel", "Shared channel", "task-a", algorithm);
    REQUIRE(first);
    REQUIRE(second);
    CHECK(FindAction(first, BAStreamChannel_Code) == FindAction(second, BAStreamChannel_Code));
    CHECK(FindAction(first, AADetect_Code) == FindAction(second, AADetect_Code));
    CHECK(FindAction(first, AAClassify_Code) != FindAction(second, AAClassify_Code));
    CHECK(FindAction(first, BAFilter_Code) != FindAction(second, BAFilter_Code));

    const std::weak_ptr<AlgActionBase> shared_channel   = FindAction(first, BAStreamChannel_Code);
    const std::weak_ptr<AlgActionBase> shared_detector  = FindAction(first, AADetect_Code);
    const std::weak_ptr<AlgActionBase> first_classifier = FindAction(first, AAClassify_Code);
    const std::weak_ptr<AlgActionBase> first_filter     = FindAction(first, BAFilter_Code);
    std::vector<AlgActionDataQueueStatus> statuses;
    task_base.QueueStatus(statuses);
    REQUIRE(statuses.size() == 7);
    CHECK(statuses[2].actionId == AADetect_Code);
    CHECK(statuses[2].taskIds == std::vector<std::string>{"task-z", "task-a"});
    // Map managers retain key order, while vector managers retain creation order.
    CHECK(statuses[3].actionId == AAClassify_Code);
    CHECK(statuses[3].taskIds == std::vector<std::string>{"task-a"});
    CHECK(statuses[4].taskIds == std::vector<std::string>{"task-z"});
    CHECK(statuses[5].actionId == BAFilter_Code);
    CHECK(statuses[5].taskIds == std::vector<std::string>{"task-z"});
    CHECK(statuses[6].taskIds == std::vector<std::string>{"task-a"});

    REQUIRE(task_base.TaskDelete(first));
    CHECK(first_classifier.expired());
    CHECK(first_filter.expired());
    CHECK_FALSE(shared_channel.expired());
    CHECK_FALSE(shared_detector.expired());
    CHECK(task_base.GetChannelTasks("shared-channel") == std::vector<std::string>{"task-a"});
    statuses.clear();
    task_base.QueueStatus(statuses);
    REQUIRE(statuses.size() == 5);
    CHECK(statuses[2].actionId == AADetect_Code);
    CHECK(statuses[3].actionId == AAClassify_Code);
    CHECK(statuses[4].actionId == BAFilter_Code);
    for (size_t i = 2; i < statuses.size(); ++i) {
        CHECK(statuses[i].taskIds == std::vector<std::string>{"task-a"});
    }

    REQUIRE(task_base.TaskDelete(second));
    CHECK(shared_channel.expired());
    CHECK(shared_detector.expired());
    CHECK_FALSE(task_base.GetChannelInst("shared-channel"));
    statuses.clear();
    std::vector<ActionRuntimeInfo> infos;
    task_base.QueueStatus(statuses);
    task_base.ActionInfo(infos);
    CHECK(statuses.empty());
    CHECK(infos.empty());
}

}  // namespace cosmo
