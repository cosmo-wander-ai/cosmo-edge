// Stream-analysis task base — manages action orchestration, lifecycle and channel bindings.

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include "flow/action/ActionBranchMng.h"
#include "flow/action/ActionParam.h"
#include "flow/action/AlgActionBase.h"
#include "flow/alarm/AreaAlarmMng.h"
#include "flow/alarm/TaskAlarmMng.h"
#include "flow/alarm/TaskFaceAlarmMng.h"
#include "flow/channel/AlgChannelMng.h"
#include "flow/classify/AiClassifyAreaMng.h"
#include "flow/classify/AiClassifyAttrMng.h"
#include "flow/classify/AiClassifyGroupMng.h"
#include "flow/classify/AiClassifyMng.h"
#include "flow/common/AlgDataQueueDistributor.h"
#include "flow/detect/AiDetectMng.h"
#include "flow/detect/DinoDetectMng.h"
#include "flow/landmark/AiLandmarkMng.h"
#include "flow/logical/FaceLogicMng.h"
#include "flow/logical/LogicalJudgmentMng.h"
#include "flow/qwen3vl/Qwen3VLMng.h"
#include "flow/recognizer/AiRecognizerMng.h"
#include "flow/sam2/Sam2SegmentMng.h"
#include "flow/sensitivity/PosSaveSensitivityMng.h"
#include "flow/sensitivity/SensitivityMng.h"
#include "flow/target/TargetChooseBestMng.h"
#include "flow/target/TargetFilterMng.h"
#include "flow/track/AiTrackMng.h"
#include "flow/video/AiVideoQualityMng.h"
#include "util/dto/ServerMsgTypes.h"

namespace cosmo {

// Action create/destroy handler
struct ActionHandler {
    std::function<AlgActionBasePtr(const std::string& channelId, const std::string& taskId,
                                   ActionNode& action)>
        create;
    std::function<bool(AlgActionBasePtr inst, const std::string& channelId, const std::string& taskId)>
        destroy;
};

struct TaskAction {
    ActionNode action;              // Orchestration parameters
    AlgActionBasePtr actionInst;    // Current action instance
    AlgActionBasePtr fatherAction;  // Parent node instance
};

struct TaskElement {
    bool is_started{false};
    std::string channelId;           // Channel ID
    std::string channelName;         // Channel name
    std::string taskId;              // Task ID globally unique
    std::string flowActionId{"-1"};  // Flow action ID of the root action
    std::deque<TaskAction> actions;  // Algorithm orchestration instances
    MsgTaskConfig params;            // Algorithm parameters
    ActionAlgPtr action_alg;         // Algorithm orchestration configuration

    // Convenience getter (from action_alg)
    std::string GetAlgName() const {
        return action_alg ? action_alg->algorithmName : "";
    }
    std::string GetAlgId() const {
        return action_alg ? action_alg->algorithmCode : "";
    }
    std::string GetCategory() const {
        return action_alg ? action_alg->category : "";
    }
    std::string GetVersion() const {
        return action_alg ? action_alg->algorithmUpdateTime : "";
    }
};
using TaskElementPtr = std::shared_ptr<TaskElement>;

class TaskBase {
public:
    TaskBase();
    ~TaskBase();

    void QueueStatus(std::vector<AlgActionDataQueueStatus>& queStatus, unsigned int durationSec = 30);
    void ActionInfo(std::vector<ActionRuntimeInfo>& actionInfo);

    bool LogicTest(const std::string& taskId, const MsgTarget& target);

    TaskElementPtr TaskCreate(const std::string& channelId, const std::string& channelName,
                              const std::string& taskId, ActionAlgPtr actionAlg);
    bool TaskDelete(TaskElementPtr task);
    bool TaskStart(TaskElementPtr task);
    bool TaskStop(TaskElementPtr task);
    bool TaskIsStart(TaskElementPtr task);

    // Set task parameters to algorithm instances
    bool ModifyTaskParam(TaskElementPtr task, MsgTaskConfig& param);

    [[nodiscard]] AlgChannelPtr GetChannelInst(const std::string& channelId);
    [[nodiscard]] std::vector<std::string> GetChannelTasks(const std::string& channelId);

    [[nodiscard]] TaskAlarmPtr GetAlarmInst(const std::string& channelId, const std::string& taskId);

    void AddTaskChannel(const MsgTaskCreateRecv& taskCreateRecv);

    void DeleteTaskChannel(const MsgTaskCancleRecv& taskCancleRecv, const std::string& channelId,
                           const std::string& algorithmId);
    void GetCameraInfo(std::vector<MsgCameraInfo>& cameraInfos);

private:
    void TaskRegist(TaskElementPtr task);
    void TaskUnRegist(TaskElementPtr task);
    void RegisterActionHandlers();
    void RegisterMngProviders();

private:
    mutable std::shared_mutex mtx_;
    std::unordered_map<std::string_view, ActionHandler> action_handlers_;
    std::vector<IMngStatusProvider*> mng_providers_;  // QueueStatus/ActionInfo unified traversal

    AlgChannelMng channel_mng_;                // Channel/Camera management instance
    AiDetectMng detect_mng_;                   // Detection management instance
    DinoDetectMng dino_detect_mng_;            // Dino detection visual large model management instance
    Qwen3VLMng qwen3_vl_mng_;                  // Qwen3VL language visual large model management instance
    Sam2SegmentMng sam2_segment_mng_;          // Sam2 segmentation large model management instance
    AiTrackMng track_mng_;                     // Tracking management instance
    AiClassifyMng classify_mng_;               // Classification management instance
    AiClassifyGroupMng classify_group_mng_;    // Group classification management instance
    AiClassifyAreaMng classify_area_mng_;      // Area classification management instance
    AiClassifyAttrMng classify_attr_mng_;      // Attribute management instance
    AiLandmarkMng landmark_mng_;               // Landmark management instance
    AiRecognizerMng recognizer_mng_;           // Recognition management instance
    AiVideoQualityMng ai_video_quality_mng_;   // Video diagnosis management instance
    TargetFilterMng filter_mng_;               // Filtering management instance
    LogicalJudgmentMng logical_judgment_mng_;  // Logical judgment management instance
    SensitivityMng sensitivity_mng_;           // Sensitivity calculation management instance
    PosSaveSensitivityMng pos_save_sensitivity_mng_;  // Sensitivity calculation management instance
    TaskAlarmMng task_alarm_mng_;                     // Alarm management instance
    AreaAlarmMng area_alarm_mng_;                     // Area alarm judgment management instance
    FaceLogicMng face_logic_mng_;                 // Face snapshot logic: real-time snapshot, optimal snapshot
    TaskFaceAlarmMng task_face_alarm_mng_;        // Behavior-based face comparison
    ActionBranchMng action_branch_mng_;           // Branch judgment management instance
    TargetChooseBestMng target_choose_best_mng_;  // Target selection

    // Adapters for MapActionMng derived classes (declaration order must be after corresponding Mng)
    MngStatusAdapter<AiClassifyMng> classify_mng_adapter_{classify_mng_};
    MngStatusAdapter<AiClassifyGroupMng> classify_group_mng_adapter_{classify_group_mng_};
    MngStatusAdapter<AiClassifyAreaMng> classify_area_mng_adapter_{classify_area_mng_};
    MngStatusAdapter<AiClassifyAttrMng> classify_attr_mng_adapter_{classify_attr_mng_};
    MngStatusAdapter<AiLandmarkMng> landmark_mng_adapter_{landmark_mng_};
    MngStatusAdapter<AiVideoQualityMng> ai_video_quality_mng_adapter_{ai_video_quality_mng_};
    MngStatusAdapter<AiTrackMng> track_mng_adapter_{track_mng_};
    MngStatusAdapter<TargetChooseBestMng> target_choose_best_mng_adapter_{target_choose_best_mng_};
};

using TaskBasePtr = std::shared_ptr<TaskBase>;
}  // namespace cosmo
