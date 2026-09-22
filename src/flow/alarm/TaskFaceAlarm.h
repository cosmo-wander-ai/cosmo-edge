#pragma once

#include <atomic>
#include <list>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "flow/action/AlgActionBase.h"
#include "flow/channel/AlgChannel.h"
#include "flow/common/AlgDataQueueDistributor.h"
#include "flow/overview/OverviewRecordAlarmRst.h"
#include "flow/task/TaskBaseParam.h"
#include "util/AlarmImagePrivacy.h"
#include "util/MsgDynamicElement.h"
#include "util/Thread.h"
#include "util/dto/ClientMsgEvent.h"

namespace cosmo {
// Alarm is the terminal node — pushes alarms directly, no downstream queue needed
class TaskFaceAlarm : public AlgActionBase {
public:
    TaskFaceAlarm(const std::string &channelId, const std::string &taskId, const std::string &algId,
                  const std::string &algName, ActionNode &action);
    ~TaskFaceAlarm();

    void QueueStatus(std::vector<AlgActionDataQueueStatus> &queStatus,
                     unsigned int durationSec = 30) override;
    void ActionInfo(std::vector<ActionRuntimeInfo> &actionInfo) override;

    void ConfigurePrivacyDetectors(const std::vector<std::string> &detectorIds);

    // Modify parameters — incremental update on existing params
    bool ModifyParam(const std::string &channelId, const std::string &taskId,
                     std::vector<MsgDynamicKeyValue> &params) override;
    // Set parameters — clear previous params and apply full replacement
    bool SetParam(const std::string &channelId, const std::string &taskId,
                  std::vector<MsgDynamicKeyValue> &params) override;
    // Set areas — clear previous areas and apply full replacement
    bool SetArea(const std::string &channelId, const std::string &taskId, std::vector<MsgTaskArea> &areas,
                 std::vector<MsgTaskArea> &shieldedAreas) override;
    // GetQueue() is provided by AlgActionBase (returns shared_ptr)
    void HandFrame(AlgDataPtr algData) override;

    std::string GetTaskId() {
        return m_taskId;
    };

private:
    void EventFaceRecord(const CMsgFaceEventReq &eventData);

    bool FillAlarmData(AlgDataPtr algData);

    bool AnalysisKey(MsgDynamicKeyValue &param);

    void HandPicture(CMsgFaceEventReq &msg, AlgDataPtr algData, DataAlarmUnit &alarmUnit);
    void UploadImage(const std::string &recordId, std::vector<uint8_t> &data, const std::string &url,
                     const std::string &sign);

    void HandFace(CMsgFaceEventReq &msg, AlgDataPtr algData, DataAlarmUnit &alarmUnit);

    std::shared_mutex m_mtx;  // Parameter read-write lock
    util::AlarmImagePrivacy m_imagePrivacy;
    std::vector<std::string> m_privacyExpectedDetectors;
    std::string m_taskId;
    std::string m_algId;    // Business algorithm ID
    std::string m_algName;  // Business algorithm name
    size_t m_filterFrames{0};
    size_t m_handleFrames{0};
    std::atomic<size_t> m_alarmCount{0};
    // TaskFaceAlarmParam m_param;
    TaskBaseArea m_taskArea;
};
using TaskFaceAlarmPtr = std::shared_ptr<TaskFaceAlarm>;
}  // namespace cosmo
