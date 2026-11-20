// Camera task type definitions — shared by CameraServiceImpl and CameraTaskUnit.
// Extracted from CameraTaskMng.h during inline refactoring.

#pragma once

#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "service/camera/impl/CameraTaskUnit.h"
#include "util/dto/CosmoFwd.h"
#include "util/dto/TaskCreateTypes.h"
#include "util/dto/TaskStatusDto.h"

namespace cosmo {

inline bool HasTaskInitializationFailure(const TaskStatus& status) {
    return std::any_of(status.queStatus.begin(), status.queStatus.end(), [](const auto& action) {
        return action.actionStatus == util::ErrorEnum::AI_INST_CREATEFAILED ||
               action.actionStatus == util::ErrorEnum::AI_INIT_PARAMERR;
    });
}

enum class CameraTaskStatus {
    kPause = -1,
    kStop,       // Stopped
    kInService,  // Running
    kAbnormal    // Abnormal
};

struct CameraTask {
    std::string task_id_{};
    std::string algorithm_code_{};         // Algorithm code
    std::string algorithm_name_{};         // Algorithm name
    std::string schedule_id_{};            // Schedule template ID
    std::string schedule_name_{};          // Schedule template name
    std::atomic<bool> is_enabled_{false};  // Enable switch
    std::atomic<CameraTaskStatus> status_{CameraTaskStatus::kStop};

    MsgTaskCreateRecv data_;   // Structured overlay data
    ActionAlgPtr action_alg_;  // Algorithm orchestration config for overlay
    CameraTaskUnitPtr task_;
};

void to_json(nlohmann::json& j, const CameraTask& v);
void from_json(const nlohmann::json& j, CameraTask& v);
using CameraTaskPtr = std::shared_ptr<CameraTask>;

}  // namespace cosmo
