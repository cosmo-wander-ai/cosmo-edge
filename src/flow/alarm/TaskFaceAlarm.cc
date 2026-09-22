// TaskFaceAlarm.cc — Face alarm action implementation.

#include "flow/alarm/TaskFaceAlarm.h"

#include <chrono>
#include <filesystem>
#include <thread>

#include "flow/alarm/AlarmImagePrivacy.h"
#include "flow/common/AlarmPrivacySnapshot.h"
#include "flow/common/AlgDataRecord.h"
#include "service/detail/ServiceRegistry.h"
#include "service/event/IAlarmRecordService.h"
#include "service/event/IEventNotifier.h"
#include "service/media/IVideoFrameCodec.h"
#include "service/media/IVideoFrameOSD.h"
#include "service/media/IVideoFrameTransform.h"
#include "service/path/IFileService.h"
#include "util/CipherUtil.h"
#include "util/FileUtil.h"
#include "util/Keys.h"
#include "util/Log.h"
#include "util/NToL.h"
#include "util/PathUtil.h"
#include "util/TimeUtil.h"
#include "util/TimingConstants.h"
#include "util/UuidUtil.h"
#include "util/dto/ClientMsgEvent.h"

static constexpr const char* kTag = "TaskFaceAlarm ";
namespace cosmo {
TaskFaceAlarm::~TaskFaceAlarm() {
    LOG_INFO("{}Task:{} Stop", kTag, m_taskId);
    AlgActionBase::Stop();
    LOG_INFO("{}Task:{} Delete", kTag, m_taskId);
}

TaskFaceAlarm::TaskFaceAlarm(const std::string& channelId, const std::string& taskId,
                             const std::string& algId, const std::string& algName, ActionNode& action)
    : AlgActionBase(AlgActionType::AlgActionBATaskFaceAlarm, action, channelId, taskId),
      m_taskId(taskId),
      m_algId(algId),
      m_algName(algName) {
    LOG_INFO("{}Task:{} Init ", kTag, m_taskId);
}

void TaskFaceAlarm::QueueStatus(std::vector<AlgActionDataQueueStatus>& queStatus, unsigned int durationSec) {
    const auto previous_size = queStatus.size();
    AlgActionBase::QueueStatus(queStatus, durationSec);
    if (queStatus.size() > previous_size) {
        queStatus.back().alarmCount = m_alarmCount.load(std::memory_order_relaxed);
    }
}

void TaskFaceAlarm::ActionInfo(std::vector<ActionRuntimeInfo>& actionInfos) {
    ActionRuntimeInfo actionInfoEl;
    actionInfoEl.actionId = GetActionId();
    // Alarm is the terminal node — no child info
    actionInfos.push_back(actionInfoEl);
}

/*
param.alarmInterval
*/
bool TaskFaceAlarm::AnalysisKey(MsgDynamicKeyValue& param) {
    if (m_imagePrivacy.Apply(param.key.ToRefString(), param.value.ToString())) {
        return m_imagePrivacy.Valid();
    }
    if (param.keys.empty()) {
        LOG_WARN(
            "ModifyParam "
            "[{}] param.keys is Empty",
            m_taskId);
        return false;
    }
    if (param.keys.size() != 2) {
        LOG_DEBUG(
            "ModifyParam "
            "[{}] Set {} Failed. key size:{}",
            m_taskId, param.key, param.keys.size());
        return false;
    }

    if (key::PARAM != param.keys[0]) {
        LOG_DEBUG(
            "ModifyParam "
            "[{}] param.keys[0] is Not {}",
            m_taskId, key::PARAM);
        return false;
    }

    // if (param.keys[1] == key::alarm::INTERVAL)
    // {
    // 	auto value = util::ParseInt(param.value);
    // 	if (value != m_param.alarmInterval)
    // 	{

    // 	}
    // }
    // else
    // {
    // //	LOG_WARN("ModifyParam ""[{}] param.key {} value {} is Unknow", m_taskId, param.key,
    // param.value); 	return false;
    // }

    return true;
}

void TaskFaceAlarm::ConfigurePrivacyDetectors(const std::vector<std::string>& detectorIds) {
    std::lock_guard<std::shared_mutex> lock(m_mtx);
    m_privacyExpectedDetectors = detectorIds;
}

// Modify parameters — incremental update on existing params
bool TaskFaceAlarm::ModifyParam(const std::string& /*channelId*/, const std::string& /*taskId*/,
                                std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(m_mtx);
    for (auto& param : params) {
        AnalysisKey(param);
    }
    LOG_INFO(
        "ModifyParam "
        "Task:{}  ",
        m_taskId);

    return false;
}

// Set parameters — clear previous params and apply full replacement
bool TaskFaceAlarm::SetParam(const std::string& /*channelId*/, const std::string& /*taskId*/,
                             std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(m_mtx);
    // Clear existing params first
    m_imagePrivacy = {};
    for (auto& param : params) {
        AnalysisKey(param);
    }

    return false;
}

// Set areas — clear previous areas and apply full replacement
bool TaskFaceAlarm::SetArea(const std::string& /*channelId*/, const std::string& taskId,
                            std::vector<MsgTaskArea>& areas, std::vector<MsgTaskArea>& shieldedAreas) {
    std::lock_guard<std::shared_mutex> lock(m_mtx);
    m_taskArea.taskId        = taskId;
    m_taskArea.areas         = areas;
    m_taskArea.shieldedAreas = shieldedAreas;
    for (auto& area : m_taskArea.areas) {
        area.iderectionType = GetDirectionTypeFromMsg(area.params);
        for (auto& assArea : area.associatedAreas) {
            assArea.iderectionType = GetDirectionTypeFromMsg(assArea.params);
        }
    }
    return true;
}

void TaskFaceAlarm::HandFrame(AlgDataPtr algData) {
    if (!algData) {
        m_filterFrames += 1;
        if (0 == m_filterFrames % 100) {
            LOG_WARN("{}[{}] Filter {} Frames", kTag, m_taskId, m_filterFrames);
        }
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }
    if (!algData->taskDataAlarm.alarmData) {
        LOG_WARN("{}[{}] AlarmData is NULL", kTag, m_taskId);
        return;
    }
    PublishAlarmPrivacySnapshot(*algData, m_taskId);
    FillAlarmData(algData);
    m_handleFrames += 1;
    LOG_INFO("{}[{}] Handle {} Frames", kTag, m_taskId, m_handleFrames);

    return;
}

bool TaskFaceAlarm::FillAlarmData(AlgDataPtr algData) {
    auto alarmData = algData->taskDataAlarm.alarmData;
    for (auto& alarmUnit : alarmData->alarms) {
        CMsgFaceEventReq eventData;

        eventData.messageId      = util::GenerateUUID();
        eventData.recordId       = alarmUnit.strTrackId;
        eventData.videoChannelId = GetChannel();
        eventData.taskId         = m_taskId;
        eventData.algorithmId    = m_algId;
        eventData.algorithmCode  = m_algId;
        eventData.algorithmName  = m_algName;

        if (algData->chanDataDec.frame) {
            eventData.timestamp = std::to_string(algData->chanDataDec.frame->GetTimestamp());
        } else {
            eventData.timestamp = std::to_string(util::GetMilliseconds());
        }

        try {
            HandPicture(eventData, algData, alarmUnit);
        } catch (const std::exception& error) {
            LOG_WARN("[{}] Face alarm image failed; preserve event: {}", m_taskId, error.what());
        } catch (...) {
            LOG_WARN("[{}] Face alarm image failed; preserve event", m_taskId);
        }
        HandFace(eventData, algData, alarmUnit);

        LOG_INFO("{}[{}] Alarm Push {}/{} Area:{}", kTag, m_taskId, eventData.messageId, eventData.recordId,
                 alarmUnit.areaId);
        action_status = util::ErrorEnum::Success;
        m_alarmCount.fetch_add(1, std::memory_order_relaxed);
        std::this_thread::sleep_for(
            timing::kOneSecondInterval);  // Sleep first; sync feature upload needed later
        EventFaceRecord(eventData);
        service::ServiceRegistry::Instance().Get<cosmo::service::IEventNotifier>().FaceEventPush(eventData);
    }
    return true;
}

void TaskFaceAlarm::EventFaceRecord(const CMsgFaceEventReq& eventData) {
    AlarmRecordUnit alarmRecordUnit;
    alarmRecordUnit.id        = eventData.messageId;
    alarmRecordUnit.timestamp = std::stol(eventData.timestamp);
    // alarmRecordUnit.algorithmId = eventData.algorithmId;
    // alarmRecordUnit.algorithmName = eventData.algorithmName;
    alarmRecordUnit.trackId = eventData.recordId;
    // util::EncodeJson(eventData.files, alarmRecordUnit.extraFiles);
    //  Face comparison
    LOG_INFO("{}", "Face comparison");
    service::ServiceRegistry::Instance().Get<service::IAlarmRecordService>().InsertFace(alarmRecordUnit);
}

void TaskFaceAlarm::HandPicture(CMsgFaceEventReq& msg, AlgDataPtr algData, DataAlarmUnit& alarmUnit) {
    util::AlarmImagePrivacy privacy;
    std::vector<std::string> expectedDetectors;
    {
        std::shared_lock<std::shared_mutex> lock(m_mtx);
        privacy           = m_imagePrivacy;
        expectedDetectors = m_privacyExpectedDetectors;
    }
    if (privacy.Requested() && expectedDetectors.empty()) {
        LOG_WARN("[{}] Omit face alarm image: detector topology is unavailable or unsupported", m_taskId);
        return;
    }
    // Original photo
    auto origImg = PrepareAlarmImage(algData->chanDataDec.frame, m_taskId, privacy, expectedDetectors);
    if (!VideoFrameValid(origImg)) {
        return;
    }
    LOG_INFO("SrcFrameType:{} copyFrameType:{}", algData->chanDataDec.frame->GetPixelFormat(),
             origImg->GetPixelFormat());

    // Detection crop photo
    util::Box cropBox;
    if (alarmUnit.haveRelated) {
        cropBox = alarmUnit.relatedBox;
    } else {
        cropBox = alarmUnit.box;
    }
    LOG_INFO("{}.{} {}x{}  {}x{} BoxCount:{}", alarmUnit.box.x, alarmUnit.box.y, alarmUnit.box.width,
             alarmUnit.box.height, cropBox.width, cropBox.height, alarmUnit.boxs.size());
    if (!cropBox.empty()) {
        auto cutImg =
            service::ServiceRegistry::Instance().Get<service::IVideoFrameTransform>().Crop(origImg, cropBox);
        auto cutJpeg =
            service::ServiceRegistry::Instance().Get<service::IVideoFrameCodec>().EncodeJpeg(cutImg);
        if (!cutJpeg.empty()) {
            msg.image = service::ServiceRegistry::Instance().Get<service::IFileService>().GetFileUrl(
                service::FileType::Image);
            UploadImage(msg.messageId, cutJpeg, msg.image, "detect");
        }
    }
}

void TaskFaceAlarm::HandFace(CMsgFaceEventReq& /*msg*/, AlgDataPtr /*algData*/, DataAlarmUnit& alarmUnit) {
    LOG_INFO("{} trackId:{} feature:{}", m_taskId, alarmUnit.trackId, alarmUnit.feature.feature.size());
}

void TaskFaceAlarm::UploadImage(const std::string& messageId, std::vector<uint8_t>& data,
                                const std::string& /*url*/, const std::string& sign) {
    std::string filePath = cosmo::path::GetRecordJsonPath();
    std::string fileName = (std::filesystem::path(filePath) / (messageId + "_" + sign + ".jpg")).string();
    auto ret             = util::WriteFile(fileName, reinterpret_cast<const std::uint8_t*>(data.data()),
                                           static_cast<int>(data.size()));
    if (false == ret) {
        LOG_WARN("write image file Failed {}", fileName);
        return;
    }
}

}  // namespace cosmo
