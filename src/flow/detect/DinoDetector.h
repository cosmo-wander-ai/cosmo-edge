// DinoDetector.h — Grounding DINO vision-language model detector.

#pragma once

#include <list>
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
    std::string taskId;
    std::string atomicCode;
    float fps{1.0};
    std::string prompt{"detect objects"};  // Detection prompt for Dino
    float boxConfidence{0.25f};            // Object detection confidence threshold
    float textConfidence{0.3f};            // Text matching confidence threshold
};

struct DinoDetectorParam {
    int paramModifySign{0};
    int paramActiveSign{-1};
    std::vector<DinoDetectorParamEl> param;
};

class DinoDetector : public AlgActionBase {
public:
    explicit DinoDetector(ActionNode &action);
    ~DinoDetector();

    bool DinoSdkInit();

    // Modify parameters — update on top of existing parameters
    bool ModifyParam(const std::string &channelId, const std::string &taskId,
                     std::vector<MsgDynamicKeyValue> &params) override;
    // Set parameters — clear previous parameters, set all new parameters
    bool SetParam(const std::string &channelId, const std::string &taskId,
                  std::vector<MsgDynamicKeyValue> &params) override;

    // Set areas (for zone-based alerts and visual overlay)
    bool SetArea(const std::string &channelId, const std::string &taskId, std::vector<MsgTaskArea> &areas,
                 std::vector<MsgTaskArea> &shieldedAreas) override;

    // Get overlay/overview information
    MsgOverviewMem GetOverviewInfo(const std::string &channelId, const std::string &taskId,
                                   int64_t streamIndex = -1, int64_t from = -1, int64_t to = -1) override;

    void QueueStatus(std::vector<AlgActionDataQueueStatus> &queStatus,
                     unsigned int durationSec = 30) override;

    // Add task when acquiring an instance
    bool AddTask(const std::string &channel_id, const std::string &task);
    // Remove task when releasing an instance
    bool RemoveTask(const std::string &channel_id, const std::string &task);

    // Check if a channel already uses this shared detector
    bool ChannelExist(const std::string &channel_id);
    // Check if a task already exists within a channel on this detector
    bool TaskExist(const std::string &channel_id, const std::string &task);
    // Check if max channel reuse count is reached
    bool TaskIsFull();
    bool TaskIsEmpty();

    size_t ChannelCount();
    size_t TaskCount();

    std::string GetAlgCode() {
        return m_algCode;
    };

protected:
    // Worker thread entry point (override from base class)
    virtual void run() override;

private:
    void HandFrames(std::vector<AlgDataPtr> algDatas);
    bool ValidKey(MsgDynamicKeyValue &param);
    bool AnalysisKey(const std::string &channelId, const std::string &taskId, MsgDynamicKeyValue &param);
    DinoDetectorParamEl FoundLocalParamByTask(const AlgTaskUnit &task);

    DinoDetectorParamEl GetTaskParams(const std::string &taskId);
    DinoDetectorParamEl GetTaskParamsUnlocked(
        const std::string &taskId);  // Lock-free version; caller must hold lock

    void TargetAddArea(AiDetectRstEl &target, TargetPosition pos, TargetAreaType type, MsgTaskArea &area,
                       int picWidth, int picHeight, bool bAssociatedArea = false,
                       const std::string &mainAreaId = "");
    void TargetAddLine(AiDetectRstEl &target, TargetPosition pos, MsgTaskArea &area, int picWidth,
                       int picHeight, bool bAssociatedArea = false, const std::string &mainAreaId = "");
    void SignTargetAreas(AlgDataPtr dataPtr, const std::string &taskId);
    void AddOverviewTask(const std::string &taskId);
    void OverviewRecord(const std::string &taskId, DataDetTrackClassifyPtr detRet);

private:
    std::string m_algCode;
    size_t m_maxReuseCount{1};                       // Max channels sharing this detector
    size_t m_batchCount{1};                          // Batch size for inference
    std::vector<DinoDetectorChannel> m_channelList;  // Channels/tasks using this detector
    bool m_detectorInstInit{false};
    int m_signRegister{0};
    DinoDetectorParam m_params;
    std::map<std::string, TaskBaseArea> m_taskAreas;
    std::map<std::string, OverviewRecordAiRstPtr> m_overviewRecInsts;

    // Detector inference instance
    DinoDetectorUnifyPtr m_detector;

    // Init retry control (prevent infinite retries after OOM)
    int m_initRetryCount{0};          // Consecutive failure count
    int64_t m_lastInitFailTimeMs{0};  // Timestamp of last failure (ms)
};
using DinoDetectorPtr = std::shared_ptr<DinoDetector>;
}  // namespace cosmo
