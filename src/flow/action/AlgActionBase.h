#pragma once

#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <string>

#include "flow/common/AlgDataQueue.h"
#include "flow/common/AlgDataQueueDistributor.h"
#include "flow/common/AlgDataUnit.h"
#include "util/DurationStat.h"
#include "util/FpsCalc.h"
#include "util/MsgBaseTypes.h"
#include "util/MsgDynamicElement.h"
#include "util/Thread.h"
#include "util/dto/AlgDataQueueTypes.h"
#include "util/dto/AlgorithmMsgTypes.h"
#include "util/dto/FilterTypes.h"
#include "util/dto/OverviewTypes.h"
#include "util/dto/TaskAreaTypes.h"
#include "util/dto/TaskStatusDto.h"

namespace cosmo {

enum class AlgActionType {
    AlgActionNone        = 0,       // Invalid action
    AlgActionDemux       = 1,       // Demux data
    AlgActionDecode      = 2,       // Decoded YUV/RGB
    AlgActionChannelData = 3,       // Channel data YUV/RGB
    AlgActionAiDetect    = 4,       // Detection
    AlgActionAiClassify  = 5,       // Classification
    AlgActionAiTracker   = 6,       // Tracking
    AlgActionAiClassifyGroup,       // Group classification
    AlgActionAiClassifyArea,        // Area classification
    AlgActionAiClassifyAttr,        // Attribute classification data passthrough
    AlgActionAiClassifyMultPic,     // Camera moving classification
    AlgActionAiFightCluster,        // Fight clustering
    AlgActionAiFightClassify,       // Fight classification
    AlgActionAiLandmark,            // Landmark
    AlgActionAiRecognizer,          // Feature extraction
    AlgActionAiPersonFace,          // Face detection in human body
    AlgActionAiOcr,                 // OCR detection
    AlgActionAiFilter,              // Filtering
    AlgActionAiVideoQuality,        // Video diagnosis
    AlgActionBAFilter,              // Filtering
    AlgActionBALogicalJudgment,     // Logical judgment
    AlgActionBASensitivity,         // Sensitivity
    AlgActionBAPosSaveSensitivity,  // Positive detection save sensitivity
    AlgActionBATaskAlarm,           // Alarm
    AlgActionBAAreaAlarm,           // Area judgment
    AlgActionBAFaceLogic,           // Face snapshot logic
    AlgActionBAFriendDistance,      // Teammate distance judgment
    AlgActionBATaskFaceAlarm,       // Behavior-based face comparison
    AlgActionBAActionBranch,        // Branch judgment
    AlgActionBAFilterLogic,         // Filter status judgment
    AlgActionBAAssoTarget,          // Target association
    AlgActionBATargetChooseBest,    // Target selection
    AlgActionBATaskCollect,         // Collection task
    AlgActionAAIrCheck,             // Image color mode
    AlgActionDinoDetect,            // Dino detection visual large model
    AlgActionSam2Segment,           // Sam2 segmentation visual large model
    AlgActionQwen3VL,               // Qwen3 language visual large model
};

class AlgActionBase : public util::Thread {
public:
    AlgActionBase(AlgActionType actionType, ActionNode& action, std::string channel = "",
                  std::string taskId = "", std::string threadName = "");

    virtual ~AlgActionBase();

    // Start
    virtual void Start();
    // Stop
    virtual void Stop();

    // Active task reference count - used when multiple tasks share an action to determine if it can truly
    // stop
    void AddActiveTask() {
        m_activeTaskCount.fetch_add(1);
    }
    void RemoveActiveTask();
    int GetActiveTaskCount() const {
        return m_activeTaskCount.load();
    }

    float GetInFps() const {
        return in_fps.load(std::memory_order_relaxed);
    }
    size_t GetInFrames() const {
        return handle_frame_cnt.load(std::memory_order_relaxed);
    }
    std::string GetUuid() const {
        return uuid;
    }

    AlgActionType GetActionType() const;

    const std::string& GetActionId() const;

    const std::string& GetFlowActionId() const;

    const std::string GetAtomicCode() const;

    const std::string& GetChannel() const;

    const std::string& GetTaskId() const;

    const std::string& GetName() const;

    util::DurationStatInfo GetDurationInfo(int durationMs = 5000) {
        return duration_stat.ComputeStats(durationMs);
    }

    std::vector<MsgTaskArea> GetAssoAreas(const std::vector<MsgTaskArea>& areas);

    virtual float GetFps() const;

    virtual bool RegistTaskQueue(AlgTaskUnit& task);
    virtual bool RemoveTaskQueue(AlgTaskUnit& task);
    // Force-remove all distributor entries for a given taskId (cleanup during task deletion)
    virtual int ForceRemoveByTaskId(const std::string& taskId);
    virtual std::shared_ptr<AlgDataQueue<AlgDataPtr>> GetQueue();

    virtual void QueueStatus(std::vector<AlgActionDataQueueStatus>& queStatus, unsigned int durationSec = 30);
    virtual void ActionInfo(std::vector<ActionRuntimeInfo>& actionInfo);

    AlgDataQueueDistributorPtr GetDistributor() const {
        return distributor;
    }

    void SetActionAlg(ActionAlgPtr alg) {
        action_alg = alg;
    }
    virtual void RegisterTaskContext(const std::string& taskId, ActionAlgPtr alg, const ActionNode& action);
    ActionAlgPtr GetActionAlg() const {
        return action_alg;
    }

    // Modify parameters - modify based on existing parameters
    virtual bool ModifyParam(const std::string& channelId, const std::string& taskId,
                             std::vector<MsgDynamicKeyValue>& params);
    // Set parameters - clear previous parameters and set new ones completely
    virtual bool SetParam(const std::string& channelId, const std::string& taskId,
                          std::vector<MsgDynamicKeyValue>& params);

    virtual bool SetArea(const std::string& channelId, const std::string& taskId,
                         std::vector<MsgTaskArea>& areas, std::vector<MsgTaskArea>& shieldedAreas);

    // Get historical info from: start frame to: end frame
    virtual std::vector<DataDetTrackClassify> GetHistory(const std::string& channelId,
                                                         const std::string& taskId, int64_t from, int64_t ts,
                                                         int64_t to);

    // Get overlay info
    virtual MsgOverviewMem GetOverviewInfo(const std::string& channelId, const std::string& taskId,
                                           int64_t streamIndex = -1, int64_t from = -1, int64_t to = -1);

protected:
    // Subclasses override this method to process single frame data
    virtual void HandFrame(AlgDataPtr /*algData*/) {}

    // Subclasses override run
    virtual void run() override;

    AlgActionType action_type = AlgActionType::AlgActionNone;
    std::string channel;
    std::string task_id;
    ActionNode action_node;

    util::ErrorEnum action_status = util::ErrorEnum::Success;

    std::atomic<bool> running{false};

    // Number of active (started) tasks using this action; stops thread only when <= 0
    std::atomic<int> m_activeTaskCount{0};

    std::mutex lifecycle_mtx_;

    std::shared_mutex mtx;

    AlgDataQueueDistributorPtr distributor = nullptr;

    std::shared_ptr<AlgDataQueue<AlgDataPtr>> data_queue = nullptr;

    ActionAlgPtr action_alg{nullptr};

    std::atomic<size_t> handle_frame_cnt{0};

    size_t invalid_frame_cnt = 0;

    size_t frame_index = 0;

    size_t stream_index = 0;

    size_t timestamp = 0;

    std::atomic<float> in_fps{0.0f};

    std::string uuid;

    util::DurationStat duration_stat;
    util::FpsCalc input_fps_calc;
};

using AlgActionBasePtr = std::shared_ptr<AlgActionBase>;

}  // namespace cosmo
