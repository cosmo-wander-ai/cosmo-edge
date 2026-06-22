// Camera task type definitions — shared by CameraServiceImpl and CameraTaskUnit.
// Extracted from CameraTaskMng.h during inline refactoring.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "service/camera/impl/CameraTaskUnit.h"
#include "util/dto/CosmoFwd.h"
#include "util/dto/TaskCreateTypes.h"

namespace cosmo {

enum class CameraTaskOpStatus {
    kNone = 0,
    kOpenByConfig,      // Opened by config
    kOpenByInterface,   // Opened by API
    kOpenBySchedule,    // Opened by schedule template
    kOpenByAuth,        // Opened by authorization
    kCloseByConfig,     // Closed by config
    kCloseByInterface,  // Closed by API
    kCloseBySchedule,   // Closed by schedule template
    kCloseByAuth        // Closed by authorization
};
enum class CameraTaskStatus {
    kPause = -1,
    kStop,       // Stopped
    kInService,  // Running
    kAbnormal    // Abnormal
};
struct CameraTaskOp {
    int64_t op_timestamp_{0};   // Operation timestamp (not persisted)
    CameraTaskOpStatus status;  // Operation status (not persisted)
};

struct CameraTask {
    std::string task_id_{};
    std::string algorithm_code_{};  // Algorithm code
    std::string algorithm_name_{};  // Algorithm name
    std::string schedule_id_{};     // Schedule template ID
    std::string schedule_name_{};   // Schedule template name
    bool is_enabled_{false};        // Enable switch
    CameraTaskStatus status_{CameraTaskStatus::kStop};
    std::vector<CameraTaskOp> ops_;  // Operation log (max 10 entries, not persisted)

    MsgTaskCreateRecv data_;   // Structured overlay data
    ActionAlgPtr action_alg_;  // Algorithm orchestration config for overlay
    CameraTaskUnitPtr task_;
};

void to_json(nlohmann::json& j, const CameraTask& v);
void from_json(const nlohmann::json& j, CameraTask& v);
using CameraTaskPtr = std::shared_ptr<CameraTask>;

}  // namespace cosmo
