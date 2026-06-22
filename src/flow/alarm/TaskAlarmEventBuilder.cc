// TaskAlarmEventBuilder.cc — Alarm event construction and dispatch.
// Refactored from a single 208-line FillAlarmData() into focused helpers:
// ShouldFilterAlarm()   — alarm filtering (interval/count/position/LLM)
// BuildBaseEventData()  — base event field population
// AttachAlarmMedia()    — recording + picture attachment
// FillEventProperty()   — property type dispatch
// DispatchAlarmEvent()  — WebSocket/HTTP/linkage push

#include <filesystem>

#include "flow/alarm/TaskAlarm.h"
#include "flow/alarm/TaskAlarmInternalTypes.h"
#include "service/detail/ServiceRegistry.h"
#include "service/event/IAlarmRecordService.h"
#include "service/event/IEventNotifier.h"
#include "service/infra/ILinkageService.h"
#include "service/system/IConfigReadService.h"
#include "util/JsonStructUtil.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/TimeUtil.h"
#include "util/UuidUtil.h"

namespace chrono = std::chrono;

static constexpr const char* kTag = "TaskAlarm ";

namespace cosmo {

// ---------------------------------------------------------------------------
// ShouldFilterAlarm — returns true if the alarm should be skipped
// ---------------------------------------------------------------------------
bool TaskAlarm::ShouldFilterAlarm(const AlgDataPtr& algData, const DataAlarmUnit& alarmUnit,
                                  const chrono::steady_clock::time_point& now, const AreaIdData& areaData,
                                  AlarmIdData& idData, MsgRecAlarm& recAlarmData) {
    // Area-level alarm interval
    if (m_alarmCount > 0) {
        if ((areaData.haveReport) && (m_param.alarmInterval > 0) &&
            (now - areaData.lastAlarmTime < chrono::milliseconds(m_param.alarmInterval * 1000))) {
            recAlarmData.filterAlarmInterval = true;
            m_overviewRecInst.OverviewRecordFrame(recAlarmData);
            return true;
        }
    }

    // Per-target alarm count limit (only for tracked targets)
    if ((alarmUnit.trackId >= 0) && (m_param.targetAlarmCount > 0) &&
        (idData.alarmCount >= m_param.targetAlarmCount)) {
        idData.alarmCount += 1;
        recAlarmData.filterTargetAlarmCount = true;
        m_overviewRecInst.OverviewRecordFrame(recAlarmData);
        return true;
    }

    // Per-target alarm interval
    if (idData.alarmCount > 0) {
        if ((alarmUnit.trackId >= 0) && (m_param.targetAlarmInterval > 0) &&
            (now - idData.lastAlarmTime < chrono::milliseconds(m_param.targetAlarmInterval * 1000))) {
            recAlarmData.filterTargetAlarmInterval = true;
            m_overviewRecInst.OverviewRecordFrame(recAlarmData);
            return true;
        }
    }

    // Position-based suppression
    if (!CheckReportAlarm(alarmUnit.box, m_param.restrainSwitch, m_param.overlapRate,
                          m_param.restrainTime * 3600)) {
        recAlarmData.filterPosition = true;
        m_overviewRecInst.OverviewRecordFrame(recAlarmData);
        return true;
    }

    // LLM review (trigger-type events only)
    if (m_param.enableLlmReview && !alarmUnit.bLlmPrejudged &&
        alarmUnit.reportType == OnEventsReportType::Trigger) {
        if (!LlmReviewAlarm(alarmUnit, algData->chanDataDec.frame)) {
            LOG_INFO("{}[{}] trackId:{} Alarm Filter By LLM Review", kTag, task_id, alarmUnit.trackId);
            recAlarmData.filterLlmReview = true;
            m_overviewRecInst.OverviewRecordFrame(recAlarmData);
            return true;
        }
    }

    return false;
}

// ---------------------------------------------------------------------------
// BuildBaseEventData — populate common event fields
// ---------------------------------------------------------------------------
CMsgOnEventsReq TaskAlarm::BuildBaseEventData(const AlgDataPtr& algData, const DataAlarmUnit& alarmUnit) {
    CMsgOnEventsReq eventData;

    eventData.messageId      = util::GenerateUUID();
    eventData.recordId       = alarmUnit.strTrackId;
    eventData.taskId         = m_param.taskId.empty() ? task_id : m_param.taskId;
    eventData.videoChannelId = m_param.videoChannelId.empty() ? GetChannel() : m_param.videoChannelId;
    eventData.channelName    = GetChannelName();
    eventData.areaId         = alarmUnit.areaId;
    eventData.algorithmId    = GetAlgId();
    eventData.algorithmCode  = GetAlgId();
    eventData.algorithmName  = GetAlgName();
    eventData.areaName       = alarmUnit.areaName;

    if (algData->chanDataDec.frame) {
        if (algData->chanDataDec.reportTimeStamp) {
            eventData.timestamp  = std::to_string(algData->chanDataDec.reportTimeStamp);
            eventData.itimestamp = algData->chanDataDec.reportTimeStamp;
        } else {
            eventData.timestamp  = std::to_string(algData->chanDataDec.frame->GetTimestamp());
            eventData.itimestamp = algData->chanDataDec.frame->GetTimestamp();
        }
    } else {
        eventData.timestamp  = std::to_string(util::GetMilliseconds());
        eventData.itimestamp = util::GetMilliseconds();
    }

    return eventData;
}

// ---------------------------------------------------------------------------
// AttachAlarmMedia — recording trigger + alarm picture
// ---------------------------------------------------------------------------
void TaskAlarm::AttachAlarmMedia(CMsgOnEventsReq& eventData, const AlgDataPtr& algData,
                                 DataAlarmUnit& alarmUnit) {
    if (!algData->chanDataDec.frame) {
        return;
    }

    if ((OnEventsPropertyType::None == m_propertyType) ||
        (OnEventsPropertyType::PersonCount == m_propertyType) ||
        ((OnEventsReportType::Trigger == alarmUnit.reportType) && (m_param.triggerEventRecordType)) ||
        ((OnEventsReportType::Realtime == alarmUnit.reportType) && (m_param.realtimeEventRecordType))) {
        auto jsonFile          = RecordVideoJson(eventData);
        int64_t frameSeq       = algData->chanDataDec.frame->GetFrameIndex();
        int64_t streamIndex    = algData->chanDataDec.frame->GetStreamIndex();
        int64_t frameTimestamp = algData->chanDataDec.frame->GetTimestamp();

        if (service::ServiceRegistry::Instance()
                .Get<service::IConfigReadService>()
                .GetAlarmVideoDuration()
                .bopen) {
            eventData.video =
                RecordMp4(alarmUnit.trackId, eventData.messageId, frameSeq, streamIndex, frameTimestamp,
                          eventData.videostructured, jsonFile, alarmUnit.retroDirect, eventData.overviewFile);
        }
    }

    // Attach alarm picture (skip for count-type alarms)
    if (!((OnEventsPropertyType::CountNumber == m_propertyType) ||
          (OnEventsPropertyType::People == m_propertyType) ||
          (OnEventsPropertyType::Car == m_propertyType))) {
        HandPicture(eventData, algData, alarmUnit);
    }
}

// ---------------------------------------------------------------------------
// FillEventProperty — dispatch property population by type
// ---------------------------------------------------------------------------
void TaskAlarm::FillEventProperty(CMsgOnEventsReq& eventData, AlgDataPtr& algData, DataAlarmUnit& alarmUnit,
                                  AlarmIdData& idData) {
    if (OnEventsPropertyType::Face == m_propertyType) {
        eventData.bHaveProperty = true;
        HandFace(eventData, algData, alarmUnit);
    } else if (OnEventsPropertyType::CountNumber == m_propertyType) {
        eventData.bHaveProperty        = true;
        eventData.property.type        = OnEventsPropertyType::CountNumber;
        eventData.property.countNumber = alarmUnit.boxs.size();
        if (alarmUnit.statusChange) {
            HandPicture(eventData, algData, alarmUnit);
        }
    } else if (OnEventsPropertyType::People == m_propertyType) {
        eventData.bHaveProperty               = true;
        eventData.property.type               = m_propertyType;
        eventData.property.people.enterNumber = alarmUnit.passFlowData.enterNumber;
        eventData.property.people.leaveNumber = alarmUnit.passFlowData.leaveNumber;
        eventData.property.people.leaveOrgNum = alarmUnit.passFlowData.leaveOrgNum;
        eventData.property.people.enterOrgNum = alarmUnit.passFlowData.enterOrgNum;
        eventData.property.people.time        = alarmUnit.passFlowData.time;
    } else if (OnEventsPropertyType::Car == m_propertyType) {
        eventData.bHaveProperty            = true;
        eventData.property.type            = m_propertyType;
        eventData.property.car.enterNumber = alarmUnit.passFlowData.enterNumber;
        eventData.property.car.leaveNumber = alarmUnit.passFlowData.leaveNumber;
        eventData.property.car.leaveOrgNum = alarmUnit.passFlowData.leaveOrgNum;
        eventData.property.car.enterOrgNum = alarmUnit.passFlowData.enterOrgNum;
        eventData.property.car.time        = alarmUnit.passFlowData.time;
    } else if (OnEventsPropertyType::PersonCount == m_propertyType) {
        eventData.bHaveProperty        = true;
        eventData.property.type        = m_propertyType;
        eventData.property.personCount = alarmUnit.boxs.size();
    } else if (OnEventsPropertyType::BodyFeature == m_propertyType) {
        eventData.bHaveProperty = true;
        HandBodyFeature(eventData, algData, alarmUnit, idData);
    } else if (OnEventsPropertyType::MachineMaterial == m_propertyType) {
        eventData.bHaveProperty = true;
        eventData.property.type = m_propertyType;
        if (IsValidFilterStatus(static_cast<int>(alarmUnit.filterStatus))) {
            eventData.property.machineMaterial.runningStatus =
                std::to_string(static_cast<int>(alarmUnit.filterStatus));
        }
        eventData.property.machineMaterial.groupId      = alarmUnit.matchInfo.group_id;
        eventData.property.machineMaterial.groupName    = alarmUnit.matchInfo.group_name;
        eventData.property.machineMaterial.matchId      = alarmUnit.matchInfo.match_id;
        eventData.property.machineMaterial.matchDegree  = alarmUnit.matchInfo.match_degree;
        eventData.property.machineMaterial.baseImageUrl = alarmUnit.matchInfo.base_image_url;
    } else if (OnEventsPropertyType::WorkClothesRecognition == m_propertyType) {
        eventData.bHaveProperty                                = true;
        eventData.property.type                                = m_propertyType;
        eventData.property.workClothesRecognition.groupId      = alarmUnit.matchInfo.group_id;
        eventData.property.workClothesRecognition.groupName    = alarmUnit.matchInfo.group_name;
        eventData.property.workClothesRecognition.matchId      = alarmUnit.matchInfo.match_id;
        eventData.property.workClothesRecognition.matchDegree  = alarmUnit.matchInfo.match_degree;
        eventData.property.workClothesRecognition.baseImageUrl = alarmUnit.matchInfo.base_image_url;
    } else if (OnEventsPropertyType::Vehicle == m_propertyType) {
        eventData.bHaveProperty = true;
        eventData.property.type = m_propertyType;

        for (auto attrRst : alarmUnit.attrRsts) {
            CMsgAiAttr attr;
            attr.atomicCode = attrRst.atomic_code;
            attr.label      = attrRst.label;
            attr.category   = attrRst.category;
            attr.confidence = attrRst.confidence;
            eventData.property.vehicle.attrs.push_back(attr);
        }

        eventData.property.vehicle.vehicleColor =
            std::to_string(static_cast<int>(GBVehicleColor(alarmUnit.attrRsts)));
        eventData.property.vehicle.vehicleClass = GBVehicleType(alarmUnit.attrRsts);
        eventData.property.vehicle.orientation =
            std::to_string(static_cast<int>(GBVehicleDirection(alarmUnit.attrRsts)));
        eventData.property.vehicle.plateSrc = alarmUnit.ocrString;
        size_t wordSize                     = 0;
        if (!ValidatePlate(alarmUnit.ocrString, eventData.property.vehicle.plate, wordSize)) {
            eventData.property.vehicle.plate = alarmUnit.ocrString;
        }

        HandBodyPicture(eventData, alarmUnit, idData);
        LOG_INFO("plateSrc:{} plate:{} plateSize:{} wordSize:{}", eventData.property.vehicle.plateSrc,
                 eventData.property.vehicle.plate, eventData.property.vehicle.plate.size(), wordSize);
    }

    if (VideoFrameValid(alarmUnit.baseFrame)) {
        eventData.bHaveProperty = true;
        eventData.property.type = OnEventsPropertyType::MachineMaterial;
    }
}

// ---------------------------------------------------------------------------
// DispatchAlarmEvent — push via WebSocket / HTTP / linkage
// ---------------------------------------------------------------------------
void TaskAlarm::DispatchAlarmEvent(CMsgOnEventsReq& eventData) {
    if (service::ServiceRegistry::Instance().Get<service::IConfigReadService>().IsNetworkModel()) {
        // Network model: direct push
        if (!((OnEventsPropertyType::CountNumber == m_propertyType) ||
              (OnEventsPropertyType::Car == m_propertyType) ||
              (OnEventsPropertyType::People == m_propertyType))) {
            service::ServiceRegistry::Instance().Get<cosmo::service::IEventNotifier>().WebSocketEventPush(
                eventData);
        }
        service::ServiceRegistry::Instance().Get<cosmo::service::IEventNotifier>().EventPush(eventData);
        return;
    }

    // Local model: resolve paths, push WebSocket, then HTTP with local paths
    std::string webFilePath   = cosmo::path::GetEventWebPath(eventData.itimestamp);
    std::string localFilePath = cosmo::path::GetEventPath(eventData.itimestamp);

    std::string fullPictureWebPath, fullPictureLocalPath;
    std::string detPictureWebPath, detPictureLocalPath;
    std::string origPictureWebPath, origPictureLocalPath;

    if (!eventData.fullPicture.empty()) {
        fullPictureWebPath   = (std::filesystem::path(webFilePath) / eventData.fullPicture).string();
        fullPictureLocalPath = (std::filesystem::path(localFilePath) / eventData.fullPicture).string();
    }
    if (!eventData.detectedPicture.empty()) {
        detPictureWebPath   = (std::filesystem::path(webFilePath) / eventData.detectedPicture).string();
        detPictureLocalPath = (std::filesystem::path(localFilePath) / eventData.detectedPicture).string();
    }
    if (!eventData.orignalPicture.empty()) {
        origPictureWebPath   = (std::filesystem::path(webFilePath) / eventData.orignalPicture).string();
        origPictureLocalPath = (std::filesystem::path(localFilePath) / eventData.orignalPicture).string();
    }

    // WebSocket push with web paths
    eventData.orignalPicture  = origPictureWebPath;
    eventData.detectedPicture = detPictureWebPath;
    eventData.fullPicture     = fullPictureWebPath;
    if (!((OnEventsPropertyType::CountNumber == m_propertyType) ||
          (OnEventsPropertyType::Car == m_propertyType) ||
          (OnEventsPropertyType::People == m_propertyType))) {
        service::ServiceRegistry::Instance().Get<cosmo::service::IEventNotifier>().WebSocketEventPush(
            eventData);
    }

    // Linkage trigger
    service::ServiceRegistry::Instance().Get<service::ILinkageService>().Alarm(GetChannel(), GetAlgId());

    // HTTP push with local paths
    eventData.orignalPicture  = origPictureLocalPath;
    eventData.detectedPicture = detPictureLocalPath;
    eventData.fullPicture     = fullPictureLocalPath;
    if (OnEventsPropertyType::Face == m_propertyType) {
        if (!eventData.property.face.image.empty()) {
            eventData.property.face.image = eventData.detectedPicture;
        }
        eventData.property.recognition.LibImage =
            cosmo::path::GetBaseDir() + eventData.property.recognition.LibImage;
    }

    service::ServiceRegistry::Instance().Get<cosmo::service::IEventNotifier>().EventPush(eventData);
}

// ---------------------------------------------------------------------------
// FillAlarmData — orchestrator (was 208 lines, now ~50)
// ---------------------------------------------------------------------------
bool TaskAlarm::FillAlarmData(AlgDataPtr algData) {
    auto alarmData = algData->taskDataAlarm.alarmData;
    for (auto& alarmUnit : alarmData->alarms) {
        MsgRecAlarm recAlarmData;
        recAlarmData.type        = m_propertyType;
        recAlarmData.targetCount = alarmUnit.boxs.size();
        if (algData->chanDataDec.frame) {
            recAlarmData.index       = algData->chanDataDec.frame->GetFrameIndex();
            recAlarmData.streamIndex = algData->chanDataDec.frame->GetStreamIndex();
            recAlarmData.timestamp   = algData->chanDataDec.frame->GetTimestamp();
        }
        recAlarmData.areaId  = alarmUnit.areaId;
        recAlarmData.trackId = alarmUnit.trackId;

        auto now       = chrono::steady_clock::now();
        auto& areaData = m_mapAreaIdStatus[alarmUnit.areaId];
        auto& idData   = m_mapAlarmIdStatus[alarmUnit.trackId];

        if (ShouldFilterAlarm(algData, alarmUnit, now, areaData, idData, recAlarmData))
            continue;

        // Update counters
        idData.alarmCount += 1;
        idData.lastAlarmTime   = now;
        areaData.lastAlarmTime = now;
        areaData.haveReport    = true;

        // Build event
        auto eventData = BuildBaseEventData(algData, alarmUnit);
        AttachAlarmMedia(eventData, algData, alarmUnit);
        FillEventProperty(eventData, algData, alarmUnit, idData);

        LOG_INFO("{}[{}] Alarm Push {}/{} trackId:{} Area:{} Type:{}", kTag, task_id, eventData.messageId,
                 eventData.recordId, idData.trackId, alarmUnit.areaId, m_propertyType);
        action_status = util::ErrorEnum::Success;
        m_alarmCount += 1;
        m_lastAlarmTime = now;

        // Record and update pass-flow totals
        if (OnEventsPropertyType::People == m_propertyType) {
            recAlarmData.enterTotalCount = alarmUnit.passFlowData.enterTotalNum;
            recAlarmData.leaveTotalCount = alarmUnit.passFlowData.leaveTotalNum;
        } else if (OnEventsPropertyType::Car == m_propertyType) {
            recAlarmData.enterTotalCount = alarmUnit.passFlowData.enterTotalNum;
            recAlarmData.leaveTotalCount = alarmUnit.passFlowData.leaveTotalNum;
        }

        recAlarmData.alarm = true;
        m_overviewRecInst.OverviewRecordFrame(recAlarmData);
        EventRecord(eventData);

        // Adjust time format for dispatch
        if (OnEventsPropertyType::People == eventData.property.type) {
            eventData.property.people.time = alarmUnit.passFlowData.timeSec;
        } else if (OnEventsPropertyType::Car == eventData.property.type) {
            eventData.property.car.time = alarmUnit.passFlowData.timeSec;
        }
        eventData.category = GetAlgCategory();

        DispatchAlarmEvent(eventData);
    }
    return true;
}

void TaskAlarm::EventRecord(CMsgOnEventsReq& eventData) {
    if ((OnEventsPropertyType::CountNumber == m_propertyType)) {
        return;
    }
    AlarmRecordUnit alarmRecordUnit;
    alarmRecordUnit.id             = eventData.messageId;
    alarmRecordUnit.category       = GetAlgCategory();
    alarmRecordUnit.videoChannelId = eventData.videoChannelId;
    alarmRecordUnit.channelName    = eventData.channelName;
    alarmRecordUnit.timestamp      = eventData.itimestamp;
    alarmRecordUnit.algorithmId    = eventData.algorithmId;
    alarmRecordUnit.algorithmName  = eventData.algorithmName;
    alarmRecordUnit.areaId         = eventData.areaId;
    alarmRecordUnit.areaName       = eventData.areaName;
    alarmRecordUnit.trackId        = eventData.recordId;
    if (eventData.bHaveProperty) {
        if (OnEventsPropertyType::Face == eventData.property.type) {
            alarmRecordUnit.libPersionName   = eventData.property.recognition.matchName;
            alarmRecordUnit.libPersionNumber = eventData.property.recognition.personCode;
            alarmRecordUnit.libFacesID       = eventData.property.recognition.matchId;
        } else if (OnEventsPropertyType::Vehicle == eventData.property.type) {
            alarmRecordUnit.propStr          = eventData.property.vehicle.plate;
            alarmRecordUnit.propColor        = eventData.property.vehicle.vehicleColor;
            alarmRecordUnit.propRelatedColor = eventData.property.vehicle.plateColor;
            alarmRecordUnit.propType         = eventData.property.vehicle.vehicleClass;
            alarmRecordUnit.propDirection    = eventData.property.vehicle.orientation;
        }
    }
    if (!eventData.video.empty())
        alarmRecordUnit.videoFlag = EventRecordFlag::EventRecordVideoFlagHave;
    (void)util::EncodeJson(eventData.files, alarmRecordUnit.extraFiles);
    if (eventData.bHaveProperty) {
        if (OnEventsPropertyType::People == eventData.property.type) {
            uint64_t hour = atol(eventData.property.people.time.c_str());
            int enterNum  = eventData.property.people.enterOrgNum;
            int leaveNum  = eventData.property.people.leaveOrgNum;
            service::ServiceRegistry::Instance().Get<service::IAlarmRecordService>().InsertPassFlow(
                eventData.videoChannelId, eventData.algorithmId, hour, enterNum, leaveNum);
            return;
        } else if (OnEventsPropertyType::Car == eventData.property.type) {
            uint64_t hour = atol(eventData.property.car.time.c_str());
            int enterNum  = eventData.property.car.enterOrgNum;
            int leaveNum  = eventData.property.car.leaveOrgNum;
            service::ServiceRegistry::Instance().Get<service::IAlarmRecordService>().InsertPassFlow(
                eventData.videoChannelId, eventData.algorithmId, hour, enterNum, leaveNum);
            return;
        }
        (void)util::EncodeJson(eventData.property, alarmRecordUnit.property);
    }
    service::ServiceRegistry::Instance().Get<service::IAlarmRecordService>().Insert(alarmRecordUnit);
}

}  // namespace cosmo
