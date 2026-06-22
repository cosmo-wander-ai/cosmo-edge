// TaskAlarm.h — Alarm terminal node: pushes alarms directly.
//
// Implementation partitions (methods declared here, defined in separate .cc):
//   TaskAlarmEventBuilder.cc  — alarm event construction and dispatch
//   TaskAlarmHandler.cc       — alarm handling, tracking, area combination
//   TaskAlarmLlmReview.cc     — LLM-based alarm review
//   TaskAlarmMedia.cc         — video recording and trajectory overlay
//   TaskAlarmPicture.cc       — alarm picture generation
//   TaskAlarmUpload.cc        — image and feature upload

#pragma once

#include <list>
#include <memory>
#include <string>
#include <vector>

#include "flow/action/AlgActionBase.h"
#include "flow/alarm/TaskAlarmSuppression.h"
#include "flow/channel/AlgChannel.h"
#include "flow/common/AlgDataQueueDistributor.h"
#include "flow/overview/OverviewRecordAlarmRst.h"
#include "flow/qwen3vl/OpenAiVlmClient.h"
#include "flow/task/TaskBaseParam.h"
#include "media/VideoFrame.h"
#include "util/MsgDynamicElement.h"
#include "util/dto/ClientMsgEvent.h"

namespace cosmo {
struct TaskAlarmParam {
    int alarmInterval{-1};                // Alarm interval, -1 means no limit
    int targetAlarmInterval{-1};          // Per-target alarm interval, -1 means no limit
    int targetAlarmCount{0};              // Per-target alarm count, 0 means no limit
    bool restrainSwitch{false};           // Stationary target dedup switch
    float overlapRate{0.0};               // Stationary target overlap ratio
    int restrainTime{0};                  // Stationary target dedup duration (hours)
    bool overlayTrajectory{false};        // Overlay trajectory
    bool realtimeEventRecordType{false};  // Whether periodic events record video
    bool triggerEventRecordType{true};    // Whether triggered events record video
    float faceScaleParam{1.0};            // Face detection crop scale factor
    std::string taskId;                   // Polling task ID
    std::string videoChannelId;           // Polling task channel ID
    bool enableLlmReview{false};          // LLM review switch (matches old a9313d10)
    std::string llmAtomicCode;            // Qwen3VL atomic algorithm code
    std::string llmReviewContent;  // User-defined review content (e.g. "flame", "person without helmet")
    OpenAiVlmConfig llmOpenAiConfig;
};
// Alarm is the terminal node — pushes alarms directly, no downstream queue needed
class TaskAlarm : public AlgActionBase, public TaskAlarmSuppression {
public:
    TaskAlarm(const std::string& channelId, const std::string& taskId, ActionNode& action);
    ~TaskAlarm();

    void QueueStatus(std::vector<AlgActionDataQueueStatus>& queStatus,
                     unsigned int durationSec = 30) override;
    void ActionInfo(std::vector<ActionRuntimeInfo>& actionInfo) override;

    // Modify parameters — incremental update on existing params
    bool ModifyParam(const std::string& channelId, const std::string& taskId,
                     std::vector<MsgDynamicKeyValue>& params) override;
    // Set parameters — clear previous params and apply full replacement
    bool SetParam(const std::string& channelId, const std::string& taskId,
                  std::vector<MsgDynamicKeyValue>& params) override;
    // Set areas — clear previous areas and apply full replacement
    bool SetArea(const std::string& channelId, const std::string& taskId, std::vector<MsgTaskArea>& areas,
                 std::vector<MsgTaskArea>& shieldedAreas) override;
    void HandFrame(AlgDataPtr algData) override;

    // Get overlay information
    MsgOverviewMem GetOverviewInfo(const std::string& channelId, const std::string& taskId,
                                   int64_t streamIndex = -1, int64_t from = -1, int64_t to = -1) override;

    // Number of alarm triggers
    size_t GetAlarmTriggerCnt() {
        return m_handleFrames;
    };
    // Actual alarm count
    size_t GetAlarmRealCnt() {
        return m_alarmCount;
    };

private:
    class AlarmIdData;
    class AreaIdData;
    bool FillAlarmData(AlgDataPtr algData);

    // Extracted helpers for FillAlarmData (DEBT-C08)
    bool ShouldFilterAlarm(const AlgDataPtr& algData, const DataAlarmUnit& alarmUnit,
                           const std::chrono::steady_clock::time_point& now, const AreaIdData& areaData,
                           AlarmIdData& idData, MsgRecAlarm& recAlarmData);
    CMsgOnEventsReq BuildBaseEventData(const AlgDataPtr& algData, const DataAlarmUnit& alarmUnit);
    void AttachAlarmMedia(CMsgOnEventsReq& eventData, const AlgDataPtr& algData, DataAlarmUnit& alarmUnit);
    void FillEventProperty(CMsgOnEventsReq& eventData, AlgDataPtr& algData, DataAlarmUnit& alarmUnit,
                           AlarmIdData& idData);
    void DispatchAlarmEvent(CMsgOnEventsReq& eventData);
    std::string RecordMp4(int targetId, const std::string& recordId, const int64_t& frameSeq,
                          const int64_t& streamIndex, const int64_t& frameTimestamp, std::string jsonUrl,
                          const std::string& jsonPath, RetroDirect retroDirect, std::string& overviewUrl);
    std::string RecordVideoJson(CMsgOnEventsReq& event);
    bool AnalysisKey(MsgDynamicKeyValue& param);

    std::vector<MsgTaskArea> GetAreas();
    MsgTaskArea GetArea(const std::string& areaId);
    std::vector<std::pair<util::Point, util::Point>> GetBoxLines(util::Box box, int width, int height);

    // Get trajectory lines
    std::vector<std::pair<util::Point, util::Point>> GetTrajectory(DataAlarmUnit& alarmUnit);
    util::Point GetPos(const util::Box& box, TargetPosition targetPos);

    void HandBestInfoPicture(CMsgOnEventsReq& msg, AlgDataPtr algData, DataAlarmUnit& alarmUnit);
    void HandPicture(CMsgOnEventsReq& msg, AlgDataPtr algData, DataAlarmUnit& alarmUnit);
    void UploadImage(CMsgOnEventsReq& msg, std::vector<uint8_t>& data, const std::string& url,
                     const std::string& sign);

    // LLM review (uses ILlmInferService)
    struct LlmReviewResult {
        bool parseSuccess{false};
        bool is_valid{true};
        float confidence{0.0f};
        std::string reason;
    };
    bool InitLlmReviewer();
    std::string BuildLlmReviewPrompt(const DataAlarmUnit& alarmUnit);
    LlmReviewResult ParseLlmReviewResult(const std::string& text);
    bool LlmReviewAlarm(const DataAlarmUnit& alarmUnit, const VideoFramePtr& frame);

    bool HandFace(CMsgOnEventsReq& msg, AlgDataPtr algData, DataAlarmUnit& alarmUnit);
    void HandBodyPicture(CMsgOnEventsReq& msg, DataAlarmUnit& alarmUnit, AlarmIdData& alarmIdData);
    void HandBodyFeature(CMsgOnEventsReq& msg, AlgDataPtr algData, DataAlarmUnit& alarmUnit,
                         AlarmIdData& alarmIdData);
    std::string UploadFeature(const std::string& recordId, AiFeature& data, const std::string& url);

    void TrackAdd(AlgDataPtr algData);
    void TrackOld(AlgDataPtr algData);
    bool TrackIsDispare(AlgDataPtr algData, int trackId);

    std::pair<bool, DataAlarmUnit> findAssoAreaAlarm(const std::string& alarmFlowActionId,
                                                     const std::deque<DataAlarmUnit>& alarms,
                                                     const std::vector<MsgTaskArea>& assoAreas,
                                                     unsigned int multiAlarms);
    DataAlarmUnit AlarmDataCombineAlarm(DataAlarmUnit alarm, DataAlarmUnit assoAlarm);
    std::deque<DataAlarmUnit> AlarmDataCombineWithAsso(AlgDataPtr algData);
    std::deque<DataAlarmUnit> AlarmDataCombineNoAsso(AlgDataPtr algData);
    void AlarmDataCombine(AlgDataPtr algData);

    void EventRecord(CMsgOnEventsReq& eventData);

    std::string GetJpgFileName(CMsgOnEventsReq& msg, const std::string& sign, bool needPath = true);

    std::string GetChannelName() const;
    std::string GetAlgCategory() const;
    std::string GetAlgId() const;
    std::string GetAlgName() const;

    size_t m_filterFrames{0};
    size_t m_handleFrames{0};
    size_t m_alarmCount{0};
    std::chrono::steady_clock::time_point m_lastAlarmTime;
    OnEventsPropertyType m_propertyType{OnEventsPropertyType::None};  // Property type
    TaskAlarmParam m_param;
    TaskBaseArea m_taskArea;
    bool m_areaHaveAsso{false};    // Whether any area has associated areas
    bool m_areaAssoIsArea{false};  // Associated area is a region (not a line), requires association on alarm
    std::map<unsigned, AlarmIdData> m_mapAlarmIdStatus;
    std::map<std::string, AreaIdData> m_mapAreaIdStatus;

    OverviewRecordAlarmRst m_overviewRecInst;
    AlgChannelPtr m_channelPtr{nullptr};
    int m_width{-1};
    int m_height{-1};
};
using TaskAlarmPtr = std::shared_ptr<TaskAlarm>;
}  // namespace cosmo
