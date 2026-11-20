#include <chrono>
#include <thread>
#include <vector>

#include "catch_amalgamated.hpp"
#include "flow/alarm/TaskFaceAlarm.h"

TEST_CASE("Face alarm task ID remains distinct from base task metadata", "[task-face-alarm]") {
    class FaceAlarmWithBaseTask : public cosmo::TaskFaceAlarm {
    public:
        explicit FaceAlarmWithBaseTask(cosmo::ActionNode& action)
            : TaskFaceAlarm("channel", "alarm-task", "algorithm", "Face Alarm", action) {
            task_id = "base-task";
        }
    };

    cosmo::ActionNode action;
    FaceAlarmWithBaseTask alarm(action);
    CHECK(alarm.GetFaceAlarmTaskId() == "alarm-task");
    CHECK(static_cast<const cosmo::AlgActionBase&>(alarm).GetTaskId() == "base-task");
}

namespace {

bool WaitForProcessedFrame(cosmo::TaskFaceAlarm& alarm) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (std::chrono::steady_clock::now() < deadline) {
        std::vector<cosmo::AlgActionDataQueueStatus> statuses;
        alarm.QueueStatus(statuses, 1);
        if (!statuses.empty() && statuses.back().queueStatus.status.processCount >= 1) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

}  // namespace

TEST_CASE("TaskFaceAlarm consumes its registered queue and restarts cleanly",
          "[task-face-alarm][lifecycle]") {
    cosmo::ActionNode action;
    action.actionId     = "face-alarm-test";
    action.actionName   = "FaceAlarmTest";
    action.flowActionId = "face-alarm-flow";

    cosmo::TaskFaceAlarm alarm("channel", "task", "algorithm", "Face Alarm", action);
    auto first_queue = alarm.GetQueue();

    REQUIRE(alarm.Start());
    REQUIRE(first_queue->Insert(cosmo::AlgDataPtr{}));
    REQUIRE(WaitForProcessedFrame(alarm));

    const auto stop_start = std::chrono::steady_clock::now();
    alarm.Stop();
    REQUIRE(std::chrono::steady_clock::now() - stop_start < std::chrono::seconds(1));
    REQUIRE_NOTHROW(alarm.Stop());

    REQUIRE(alarm.Start());
    auto second_queue = alarm.GetQueue();
    REQUIRE(second_queue != first_queue);
    REQUIRE(second_queue->Insert(cosmo::AlgDataPtr{}));
    REQUIRE(WaitForProcessedFrame(alarm));
    alarm.Stop();
}
