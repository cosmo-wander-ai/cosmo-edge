// AiDetector.h — YOLO-based object detector with multi-channel sharing.

#pragma once

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "flow/action/AlgActionBase.h"
#include "flow/detect/AiDetectorFps.h"
#include "flow/overview/OverviewRecordAiRst.h"
#include "flow/task/TaskBaseParam.h"
#include "infer/AiDetectorUnify.h"

// 1. Set confidence thresholds per label.
// 2. Filter detection results by confidence and distribute to task queues.

namespace cosmo {

struct AiDetectorParamEl {
    std::string task_id;
    std::vector<AiLabelParam> label_params;
};

struct AiDetectorParam {
    int param_modify_sign{0};
    int param_active_sign{-1};
    std::vector<AiDetectorParamEl> param;
};

class AiDetector : public AlgActionBase {
public:
    explicit AiDetector(ActionNode& action);
    ~AiDetector() override;

    bool AiSdkInit();

    // Modify parameters — update on top of existing parameters
    bool ModifyParam(const std::string& channel_id, const std::string& taskId,
                     std::vector<MsgDynamicKeyValue>& params) override;
    // Set parameters — clear previous parameters, set all new parameters
    bool SetParam(const std::string& channel_id, const std::string& taskId,
                  std::vector<MsgDynamicKeyValue>& params) override;

    // Set areas — clear previous areas, set all new areas
    bool SetArea(const std::string& channel_id, const std::string& taskId, std::vector<MsgTaskArea>& areas,
                 std::vector<MsgTaskArea>& shieldedAreas) override;

    // Get history records (from: start frame, to: end frame)
    std::vector<DataDetTrackClassify> GetHistory(const std::string& channel_id, const std::string& taskId,
                                                 int64_t from, int64_t ts, int64_t to) override;

    // Get overlay/overview information
    MsgOverviewMem GetOverviewInfo(const std::string& channel_id, const std::string& taskId,
                                   int64_t streamIndex = -1, int64_t from = -1, int64_t to = -1) override;

    void QueueStatus(std::vector<AlgActionDataQueueStatus>& que_status,
                     unsigned int duration_sec = 30) override;

    void Stop() override;

    // Add task when acquiring an instance. requested_fps <= 0 means unconfigured (normalized inside).
    bool AddTask(const std::string& channel_id, const std::string& task, float requested_fps);
    // Add task when acquiring an instance (legacy: fps unconfigured).
    bool AddTask(const std::string& channel_id, const std::string& task);
    // Remove task when releasing an instance
    bool RemoveTask(const std::string& channel_id, const std::string& task);

    // Check if a channel already uses this shared detector
    bool ChannelExist(const std::string& channel_id) const;
    // Check if a task already exists within a channel on this detector
    bool TaskExist(const std::string& channel_id, const std::string& task) const;
    // Check if max channel reuse count is reached (detector shared across N channels)
    bool TaskIsFull() const;
    bool TaskIsEmpty() const;

    size_t ChannelCount() const;
    size_t TaskCount() const;

    // fps-aware placement helpers. Callers must hold the owning MultiChannelActionMng::m_mtx:
    // these read channel_list_, which is mutated under that lock (channel_list_ itself is not
    // guarded by mtx; mtx only protects task_histories_/task_areas_/overview_rec_insts_).
    float NormalizeRequestedFps(float fps) const;
    float AssignedFps() const;  // Sum of each channel's max-task-fps
    float DeltaFpsForTask(const std::string& channel_id, float requested_fps) const;
    bool CanAccept(const std::string& channel_id, float requested_fps) const;
    float GetInstanceFpsBudget() const {
        return instance_fps_budget_;
    }

    std::string GetDetectorName() const {
        return name_;
    }
    std::string GetAlgCode() const {
        return alg_code_;
    }
    std::optional<InferenceInputSize> GetInputSize() const {
        return input_size_;
    }

protected:
    // Worker thread entry point (override from base class)
    void run() override;

private:
    using AlgActionBase::GetName;

    // taskId/streamIndex keep camelCase to avoid shadowing AlgActionBase members task_id/stream_index.
    void HandFrames(std::vector<AlgDataPtr> alg_datas);
    bool ValidKey(MsgDynamicKeyValue& param);
    bool AnalysisKey(const std::string& channel_id, const std::string& taskId, MsgDynamicKeyValue& param);
    bool SetConfidenceToLocal(const std::string& channel_id, const std::string& taskId,
                              AiConfidence& confidence);
    bool SetTargetPosToLocal(const std::string& channel_id, const std::string& taskId,
                             const std::string& in_label, TargetPosition pos);
    AiDetectorParamEl FoundLocalParamByTask(const AlgTaskUnit& task);
    void FindActiveConfidence(const std::vector<AiDetectorParamEl>& shouldActiveParams);
    void CheckAndActivateConfidence();

    AlgDataPtr ConfidenceFilter(AlgDataPtr data_ptr, const std::string& taskId);
    TargetPosition GetTaskLabelPos(const std::string& label, const AiDetectorParamEl& taskLabels);
    AiDetectorParamEl GetTaskLabelParams(const std::string& taskId);
    void SignTargetAreas(AlgDataPtr data_ptr, const std::string& taskId);
    void TargetAddArea(AiDetectRstEl& target, TargetPosition pos, TargetAreaType type, MsgTaskArea& area,
                       int pic_width, int pic_height, bool associated_area = false,
                       const std::string& main_area_id = "");
    void TargetAddLine(AiDetectRstEl& target, TargetPosition pos, MsgTaskArea& area, int pic_width,
                       int pic_height, bool associated_area = false, const std::string& main_area_id = "");

    // Record detection history (with aging) for overlay information
    void RecordHistory(AlgDataPtr data_ptr, const std::string& taskId);

    void AddOverviewTask(const std::string& taskId);
    void OverviewRecord(const std::string& taskId, DataDetTrackClassifyPtr detRet);

    // Adjust processing queue size based on input frame rate
    void SetProcQueSize();

private:
    std::string alg_code_;
    std::optional<InferenceInputSize> input_size_;
    std::string name_;
    int init_retry_count_{0};
    int64_t last_init_fail_time_ms_{0};
    std::vector<std::string> labels_;
    size_t max_reuse_count_{1};                             // Max channels sharing this detector
    float instance_fps_budget_{kDefaultInstanceFpsBudget};  // Per-instance fps budget for placement
    ai_detector_fps::ReuseProfile reuse_profile_;           // fps-threshold reuse profile for placement
    size_t batch_count_{1};                                 // Batch size for inference
    std::vector<AiDetectorChannel> channel_list_;           // Channels/tasks using this detector
    bool is_detector_inst_initialized_{false};
    AiDetectorUnifyPtr detector_;  // Detector inference instance
    int sign_register_{0};         // Tracks distributor sign changes to trigger confidence re-activation
    AiDetectorParam params_;
    int active_confidence_modify_sign_{0};
    int active_confidence_active_sign_{-1};
    std::vector<AiConfidence> active_confidence_;
    std::vector<AiDetectorParamEl> active_task_confidence_;
    std::map<std::string, TaskBaseArea> task_areas_;
    std::map<std::string, std::deque<DataDetTrackClassify>> task_histories_;
    std::map<std::string, OverviewRecordAiRstPtr> overview_rec_insts_;
};
using AiDetectorPtr = std::shared_ptr<AiDetector>;
}  // namespace cosmo
