// DinoDetector.h — Grounding DINO vision-language model detector.

#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "flow/action/AlgActionBase.h"
#include "flow/overview/OverviewRecordAiRst.h"
#include "flow/task/TaskBaseParam.h"
#include "infer/DinoDetectorUnify.h"

namespace cosmo {
struct DinoDetectorChannel {
    std::string channel;
    std::vector<std::string> tasks;
};

struct DinoDetectorParamEl {
    std::string task_id;
    std::string prompt{"person"};  // Detection prompt for GroundingDINO
    float box_confidence{0.3f};    // Query/object confidence threshold
    float text_confidence{0.25f};  // Prompt-token matching threshold
};

struct DinoDetectorParam {
    int param_modify_sign{0};
    std::vector<DinoDetectorParamEl> param;
};

class DinoDetector : public AlgActionBase {
public:
    explicit DinoDetector(ActionNode& action);
    ~DinoDetector() override;

    bool DinoSdkInit();

    // Modify parameters — update on top of existing parameters
    bool ModifyParam(const std::string& channel_id, const std::string& taskId,
                     std::vector<MsgDynamicKeyValue>& params) override;
    // Set one task's parameters; preserve its prompt when an update omits it.
    bool SetParam(const std::string& channel_id, const std::string& taskId,
                  std::vector<MsgDynamicKeyValue>& params) override;

    // Set areas (for zone-based alerts and visual overlay)
    bool SetArea(const std::string& channel_id, const std::string& taskId, std::vector<MsgTaskArea>& areas,
                 std::vector<MsgTaskArea>& shielded_areas) override;

    // Get overlay/overview information
    MsgOverviewMem GetOverviewInfo(const std::string& channel_id, const std::string& taskId,
                                   int64_t streamIndex = -1, int64_t from = -1, int64_t to = -1) override;

    void QueueStatus(std::vector<AlgActionDataQueueStatus>& que_status,
                     unsigned int duration_sec = 30) override;

    // Add task when acquiring an instance
    bool AddTask(const std::string& channel_id, const std::string& task);
    // Remove task when releasing an instance
    bool RemoveTask(const std::string& channel_id, const std::string& task);

    // Check if a channel already uses this shared detector
    bool ChannelExist(const std::string& channel_id) const;
    // Check if a task already exists within a channel on this detector
    bool TaskExist(const std::string& channel_id, const std::string& task) const;
    // Check if max channel reuse count is reached
    bool TaskIsFull() const;
    bool TaskIsEmpty() const;

    size_t ChannelCount() const;
    size_t TaskCount() const;

    std::string GetAlgCode() const {
        return alg_code_;
    }

protected:
    // Worker thread entry point (override from base class)
    void run() override;

private:
    // Note: taskId/streamIndex keep camelCase to avoid shadowing AlgActionBase members task_id/stream_index.
    void HandFrames(std::vector<AlgDataPtr> alg_datas);
    bool ValidKey(const MsgDynamicKeyValue& param) const;
    bool AnalysisKey(const std::string& channel_id, const std::string& taskId,
                     const MsgDynamicKeyValue& param);
    bool ApplyParamsUnlocked(const std::string& channel_id, const std::string& taskId,
                             std::vector<MsgDynamicKeyValue>& params);
    DinoDetectorParamEl FoundLocalParamByTask(const AlgTaskUnit& task) const;

    DinoDetectorParamEl GetTaskParams(const std::string& taskId) const;
    DinoDetectorParamEl GetTaskParamsUnlocked(
        const std::string& taskId) const;  // Lock-free; caller holds lock

    void TargetAddArea(AiDetectRstEl& target, TargetPosition pos, TargetAreaType type, MsgTaskArea& area,
                       int pic_width, int pic_height, bool associated_area = false,
                       const std::string& main_area_id = "");
    void TargetAddLine(AiDetectRstEl& target, TargetPosition pos, MsgTaskArea& area, int pic_width,
                       int pic_height, bool associated_area = false, const std::string& main_area_id = "");
    void SignTargetAreas(AlgDataPtr data_ptr, const std::string& taskId);
    void AddOverviewTask(const std::string& taskId);
    void OverviewRecord(const std::string& taskId, DataDetTrackClassifyPtr det_ret);

private:
    std::string alg_code_;
    size_t max_reuse_count_{1};                      // Max channels sharing this detector
    size_t batch_count_{1};                          // Batch size for inference
    std::vector<DinoDetectorChannel> channel_list_;  // Channels/tasks using this detector
    bool is_detector_inst_init_{false};
    DinoDetectorParam params_;
    std::map<std::string, TaskBaseArea> task_areas_;
    std::map<std::string, OverviewRecordAiRstPtr> overview_rec_insts_;

    // Detector inference instance
    DinoDetectorUnifyPtr detector_;

    // Init retry control (prevent infinite retries after OOM)
    int init_retry_count_{0};            // Consecutive failure count
    int64_t last_init_fail_time_ms_{0};  // Timestamp of last failure (ms)
};
using DinoDetectorPtr = std::shared_ptr<DinoDetector>;
}  // namespace cosmo
