// AiDetector.h — YOLO-based object detector with multi-channel sharing.

#pragma once

#include <list>
#include <memory>
#include <string>
#include <vector>

#include "flow/action/AlgActionBase.h"
#include "flow/overview/OverviewRecordAiRst.h"
#include "flow/task/TaskBaseParam.h"
#include "infer/AiDetectorUnify.h"

// 1. Set confidence thresholds per label.
// 2. Filter detection results by confidence and distribute to task queues.

namespace cosmo {
struct AiDetectorChannel {
    std::string channel;
    std::vector<std::string> tasks;
};

struct AiDetectorParamEl {
    std::string taskId;
    std::vector<AiLabelParam> labelParams;
};

struct AiDetectorParam {
    int paramModifySign{0};
    int paramActiveSign{-1};
    std::vector<AiDetectorParamEl> param;
};

class AiDetector : public AlgActionBase {
public:
    explicit AiDetector(ActionNode& action);
    ~AiDetector();

    bool AiSdkInit();

    // Modify parameters — update on top of existing parameters
    bool ModifyParam(const std::string& channelId, const std::string& taskId,
                     std::vector<MsgDynamicKeyValue>& params) override;
    // Set parameters — clear previous parameters, set all new parameters
    bool SetParam(const std::string& channelId, const std::string& taskId,
                  std::vector<MsgDynamicKeyValue>& params) override;

    // Set areas — clear previous areas, set all new areas
    bool SetArea(const std::string& channelId, const std::string& taskId, std::vector<MsgTaskArea>& areas,
                 std::vector<MsgTaskArea>& shieldedAreas) override;

    // Get history records (from: start frame, to: end frame)
    std::vector<DataDetTrackClassify> GetHistory(const std::string& channelId, const std::string& taskId,
                                                 int64_t from, int64_t ts, int64_t to) override;

    // Get overlay/overview information
    MsgOverviewMem GetOverviewInfo(const std::string& channelId, const std::string& taskId,
                                   int64_t streamIndex = -1, int64_t from = -1, int64_t to = -1) override;

    void QueueStatus(std::vector<AlgActionDataQueueStatus>& queStatus,
                     unsigned int durationSec = 30) override;

    void Stop() override;

    // Add task when acquiring an instance
    bool AddTask(const std::string& channel_id, const std::string& task);
    // Remove task when releasing an instance
    bool RemoveTask(const std::string& channel_id, const std::string& task);

    // Check if a channel already uses this shared detector
    bool ChannelExist(const std::string& channel_id);
    // Check if a task already exists within a channel on this detector
    bool TaskExist(const std::string& channel_id, const std::string& task);
    // Check if max channel reuse count is reached (detector shared across N channels)
    bool TaskIsFull();
    bool TaskIsEmpty();

    size_t ChannelCount();
    size_t TaskCount();

    std::string GetName() {
        return m_name;
    };
    std::string GetAlgCode() {
        return m_algCode;
    };

protected:
    // Worker thread entry point (override from base class)
    virtual void run() override;

private:
    void HandFrames(std::vector<AlgDataPtr> algDatas);
    bool ValidKey(MsgDynamicKeyValue& param);
    bool AnalysisKey(const std::string& channelId, const std::string& taskId, MsgDynamicKeyValue& param);
    bool SetConfidenceToLocal(const std::string& channelId, const std::string& taskId,
                              AiConfidence& confidence);
    bool SetTargetPosToLocal(const std::string& channelId, const std::string& taskId,
                             const std::string& inLabel, TargetPosition pos);
    AiDetectorParamEl FoundLocalParamByTask(const AlgTaskUnit& task);
    void FindActiveConfidence(const std::vector<AiDetectorParamEl>& shouldActiveParams);
    void CheckAndActiveConfidece();

    AlgDataPtr ConfidenceFilter(AlgDataPtr dataPtr, const std::string& taskId);
    TargetPosition GetTaskLabelPos(const std::string& label, const AiDetectorParamEl& taskLabels);
    AiDetectorParamEl GetTaskLabelParams(const std::string& taskId);
    void SignTargetAreas(AlgDataPtr dataPtr, const std::string& taskId);
    void TargetAddArea(AiDetectRstEl& target, TargetPosition pos, TargetAreaType type, MsgTaskArea& area,
                       int picWidth, int picHeight, bool bAssociatedArea = false,
                       const std::string& mainAreaId = "");
    void TargetAddLine(AiDetectRstEl& target, TargetPosition pos, MsgTaskArea& area, int picWidth,
                       int picHeight, bool bAssociatedArea = false, const std::string& mainAreaId = "");

    // Record detection history (with aging) for overlay information
    void RecordHistory(AlgDataPtr dataPtr, const std::string& taskId);

    void AddOverviewTask(const std::string& taskId);
    void OverviewRecord(const std::string& taskId, DataDetTrackClassifyPtr detRet);

    // Adjust processing queue size based on input frame rate
    void SetProcQueSize();

private:
    std::string m_algCode;
    std::string m_name;
    int m_initRetryCount{0};
    int64_t m_lastInitFailTimeMs{0};
    std::vector<std::string> m_lables;
    size_t m_maxReuseCount{1};                     // Max channels sharing this detector
    size_t m_batchCount{1};                        // Batch size for inference
    std::vector<AiDetectorChannel> m_channelList;  // Channels/tasks using this detector
    bool m_detectorInstInit{false};
    AiDetectorUnifyPtr m_detector;  // Detector inference instance
    int m_signRegister{0};          // Tracks distributor sign changes to trigger confidence re-activation
    AiDetectorParam m_params;
    int m_activeConfidenceModifySign{0};
    int m_activeConfidenceActiveSign{-1};
    std::vector<AiConfidence> m_activeConfidence;
    std::vector<AiDetectorParamEl> m_activeTaskConfidence;
    std::map<std::string, TaskBaseArea> m_taskAreas;
    std::map<std::string, std::deque<DataDetTrackClassify>> m_taskHistorys;
    std::map<std::string, OverviewRecordAiRstPtr> m_overviewRecInsts;
};
using AiDetectorPtr = std::shared_ptr<AiDetector>;
}  // namespace cosmo
