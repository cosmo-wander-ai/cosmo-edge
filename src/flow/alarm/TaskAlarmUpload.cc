// TaskAlarmUpload.cc — Image upload for TaskAlarmPicture.
// Split from TaskAlarmPicture.cc to reduce file size (DEBT-007).

#include <filesystem>

#include "flow/alarm/TaskAlarm.h"
#include "flow/alarm/TaskAlarmInternalTypes.h"
#include "flow/common/AreaLineUtil.h"
#include "media/Color.h"
#include "media/VideoFrame.h"
#include "service/detail/ServiceRegistry.h"
#include "service/path/IFileService.h"
#include "service/system/IConfigReadService.h"
#include "util/CipherUtil.h"
#include "util/FileUtil.h"
#include "util/GeometricPos.h"
#include "util/Log.h"
#include "util/NToL.h"
#include "util/PathUtil.h"

static constexpr const char* kTag = "TaskAlarm ";
namespace cosmo {

bool TaskAlarm::HandFace(CMsgOnEventsReq& msg, AlgDataPtr /*algData*/, DataAlarmUnit& alarmUnit) {
    LOG_INFO("{} trackId:{} feature:{} matchDegree {}", task_id, alarmUnit.trackId,
             alarmUnit.feature.feature.size(), alarmUnit.matchInfo.match_degree);
    msg.property.type                     = OnEventsPropertyType::Face;
    msg.property.recognition.matchDegree  = alarmUnit.matchInfo.match_degree;
    msg.property.recognition.matchName    = alarmUnit.matchInfo.name;
    msg.property.recognition.matchId      = alarmUnit.matchInfo.group_id;
    msg.property.recognition.LibImage     = alarmUnit.matchInfo.base_image_url;
    msg.property.recognition.matchLibName = alarmUnit.matchInfo.group_name;
    msg.property.recognition.personId     = alarmUnit.matchInfo.person_id;
    msg.property.recognition.personCode   = alarmUnit.matchInfo.person_code;
    AiConfidence faceQuality;
    GetFaceQuality(alarmUnit.confidence, faceQuality);
    msg.property.face.quality = faceQuality.confidence;

    if (!alarmUnit.feature.feature.empty()) {
        msg.property.face.featureUrl =
            service::ServiceRegistry::Instance().Get<service::IFileService>().GetFileUrl(
                service::FileType::Feature);
        UploadFeature(msg.messageId, alarmUnit.feature, msg.property.face.featureUrl);
    }
    return true;
}

void TaskAlarm::HandBodyPicture(CMsgOnEventsReq& msg, DataAlarmUnit& alarmUnit, AlarmIdData& alarmIdData) {
    if (alarmUnit.areaInfo.bHaveIntoArea) {
        auto& alarmAreaData = alarmIdData.areasInfo[alarmUnit.areaInfo.areaId];
        if (alarmAreaData.inAreaTime == alarmUnit.areaInfo.intoAreaFrame->GetTimestamp()) {
            msg.property.body.inAreaTime         = std::to_string(alarmAreaData.inAreaTime);
            msg.property.body.inAreaFullImageUrl = alarmAreaData.inAreaFullImageUrl;
        } else {
            if (VideoFrameValid(alarmUnit.areaInfo.intoAreaFrame)) {
                msg.property.bHaveTarget       = true;
                alarmAreaData.inAreaTime       = alarmUnit.areaInfo.intoAreaFrame->GetTimestamp();
                msg.property.body.inAreaTime   = std::to_string(alarmAreaData.inAreaTime);
                msg.property.target.inAreaTime = msg.property.body.inAreaTime;
            }
        }
    }

    if (alarmUnit.areaInfo.bHaveOutArea) {
        if (VideoFrameValid(alarmUnit.areaInfo.outAreaFrame)) {
            msg.property.bHaveTarget        = true;
            msg.property.body.outAreaTime   = std::to_string(alarmUnit.areaInfo.outAreaFrame->GetTimestamp());
            msg.property.target.outAreaTime = msg.property.body.outAreaTime;
        }
    }
}

void TaskAlarm::HandBodyFeature(CMsgOnEventsReq& msg, AlgDataPtr /*algData*/, DataAlarmUnit& alarmUnit,
                                AlarmIdData& alarmIdData) {
    LOG_INFO("{} trackId:{} feature:{}", task_id, alarmUnit.trackId, alarmUnit.feature.feature.size());
    msg.property.type       = OnEventsPropertyType::BodyFeature;
    msg.property.body.type  = OnEventsBodyPropertyType::Feature;
    msg.property.body.image = msg.detectedPicture;
    AiConfidence bodyQuality;
    GetPedQuality(alarmUnit.confidence, bodyQuality);
    msg.property.body.quality = bodyQuality.confidence;

    if (!alarmUnit.feature.feature.empty()) {
        msg.property.body.feature =
            UploadFeature(msg.messageId, alarmUnit.feature, msg.property.body.featureUrl);
    }

    HandBodyPicture(msg, alarmUnit, alarmIdData);
}

std::string TaskAlarm::GetJpgFileName(CMsgOnEventsReq& msg, const std::string& sign, bool needPath) {
    std::string fileName = "_" + sign + ".jpg";
    std::string filePath = cosmo::path::GetEventPath(msg.itimestamp);
    msg.files.push_back(fileName);

    if (needPath) {
        return (std::filesystem::path(filePath) / (msg.messageId + fileName)).string();
    }

    return msg.messageId + fileName;
}

void TaskAlarm::UploadImage(CMsgOnEventsReq& msg, std::vector<uint8_t>& data, const std::string& url,
                            const std::string& sign) {
    std::string fileName = GetJpgFileName(msg, sign);
    auto ret             = util::WriteFile(fileName, reinterpret_cast<const std::uint8_t*>(data.data()),
                                           static_cast<int>(data.size()));
    if (false == ret) {
        LOG_WARN("write image file Failed {}", fileName);
        return;
    }

    if (!service::ServiceRegistry::Instance().Get<service::IConfigReadService>().IsNetworkModel()) {
        // Standalone box mode — do not upload photos
        return;
    }

    std::string bucket = "gaf_commodity";
    service::ServiceRegistry::Instance().Get<service::IFileService>().UploadFile(
        msg.messageId,
        [fileName](const std::string& taskId, bool bFinished, void* /*ptr*/) {
            if (!bFinished) {
                LOG_WARN("{} Upload {} Failed", taskId, fileName);
            } else {
                LOG_INFO("{} Upload {} Success!", taskId, fileName);
            }
        },
        nullptr, "jpg", fileName, bucket, url);
}

std::string TaskAlarm::UploadFeature(const std::string& messageId, AiFeature& data, const std::string& url) {
    std::string filePath = cosmo::path::GetRecordJsonPath();
    std::string fileName = (std::filesystem::path(filePath) / (messageId + ".feature")).string();
    util::LtonConvert(data.feature.data(), data.feature.size());
    auto featureBase64 = util::EncBase64Ex(reinterpret_cast<uint8_t*>(data.feature.data()),
                                           data.feature.size() * sizeof(float));

    auto ret = util::WriteFile(fileName, featureBase64);
    if (false == ret) {
        LOG_WARN("write feature file Failed {}", fileName);
        return featureBase64;
    }

    if (!service::ServiceRegistry::Instance().Get<service::IConfigReadService>().IsNetworkModel()) {
        // Standalone box mode — do not upload photos
        return featureBase64;
    }

    std::string bucket = "gaf_feature";
    service::ServiceRegistry::Instance().Get<service::IFileService>().UploadFile(
        messageId,
        [fileName](const std::string& taskId, bool bFinished, void* /*ptr*/) {
            if (!bFinished) {
                LOG_WARN("{} Upload {} Failed", taskId, fileName);
            } else {
                LOG_INFO("{} Upload {} Success!", taskId, fileName);
            }
        },
        nullptr, "feature", fileName, bucket, url);

    return featureBase64;
}

}  // namespace cosmo
