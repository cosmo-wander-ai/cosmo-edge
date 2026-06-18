// TaskAlarmMedia.cc — Video recording and trajectory overlay.
// Implementation partition of TaskAlarm (declared in flow/alarm/TaskAlarm.h).

#include <algorithm>
#include <filesystem>

#include "flow/alarm/TaskAlarm.h"
#include "flow/alarm/TaskAlarmInternalTypes.h"
#include "service/detail/ServiceRegistry.h"
#include "service/path/IFileService.h"
#include "service/system/IConfigReadService.h"
#include "service/task/ITaskChannel.h"
#include "util/FileUtil.h"
#include "util/GeometricPos.h"
#include "util/JsonStructUtil.h"
#include "util/Log.h"
#include "util/PathUtil.h"

static constexpr const char* kTag = "TaskAlarm ";

namespace cosmo {

std::string TaskAlarm::RecordMp4(int targetId, const std::string& messageId, const int64_t& frameSeq,
                                 const int64_t& streamIndex, const int64_t& frameTimestamp,
                                 std::string jsonUrl, const std::string& jsonPath, RetroDirect retroDirect,
                                 std::string& overviewUrl) {
#ifdef DURATION_LOG
    auto timpointBegin = std::chrono::high_resolution_clock::now();
#endif

    if (!m_channelPtr) {
        m_channelPtr =
            service::ServiceRegistry::Instance().Get<cosmo::service::ITaskChannel>().GetChannelInst(
                GetChannel());
        if (!m_channelPtr) {
            LOG_WARN("{}[{}] Record But Cant Get Recoder", kTag, task_id);
            return "";
        }
    }

#ifdef DURATION_LOG
    auto timpointFileUrl = std::chrono::high_resolution_clock::now();
#endif
    std::string fileUrl;

    if (service::ServiceRegistry::Instance().Get<service::IConfigReadService>().IsNetworkModel()) {
        fileUrl = service::ServiceRegistry::Instance().Get<service::IFileService>().GetFileUrl(
            service::FileType::Video);
        jsonUrl = service::ServiceRegistry::Instance().Get<service::IFileService>().GetFileUrl(
            service::FileType::Json);
        overviewUrl = service::ServiceRegistry::Instance().Get<service::IFileService>().GetFileUrl(
            service::FileType::Json);
    }

    RecordParam recordParam;
    recordParam.channelId      = GetChannel();
    recordParam.taskId         = task_id;
    recordParam.targetId       = targetId;
    recordParam.recordId       = messageId;
    recordParam.url            = fileUrl;
    recordParam.jsonUrl        = jsonUrl;
    recordParam.jsonPath       = jsonPath;
    recordParam.overviewUrl    = overviewUrl;
    recordParam.streamIndex    = streamIndex;
    recordParam.frameSeq       = frameSeq;
    recordParam.frameTimestamp = frameTimestamp;
    recordParam.startFrameSeq  = 0;
    recordParam.retroDirect    = retroDirect;
#ifdef DURATION_LOG
    auto timpointRecordMp4 = std::chrono::high_resolution_clock::now();
#endif
    m_channelPtr->RecordMp4(recordParam);
#ifdef DURATION_LOG
    auto timpointEnd = std::chrono::high_resolution_clock::now();

    LOG_INFO(
        "Duration: Prepare:{} FileUrl:{} RecordMp4:{} ",
        std::chrono::duration_cast<std::chrono::milliseconds>(timpointFileUrl - timpointBegin).count(),
        std::chrono::duration_cast<std::chrono::milliseconds>(timpointRecordMp4 - timpointFileUrl).count(),
        std::chrono::duration_cast<std::chrono::milliseconds>(timpointEnd - timpointRecordMp4).count());
#endif

    return fileUrl;
}

std::vector<std::pair<util::Point, util::Point>> TaskAlarm::GetTrajectory(DataAlarmUnit& alarmUnit) {
    std::vector<std::pair<util::Point, util::Point>> lines;
    if (alarmUnit.targetHistory.size() < 2) {
        return lines;
    }
    if (!m_param.overlayTrajectory) {
        return lines;
    }

    for (size_t index = 1; index < alarmUnit.targetHistory.size(); ++index) {
        auto lastPoint =
            GetPos(alarmUnit.targetHistory[index - 1].box, alarmUnit.targetHistory[index - 1].targetPos);
        auto currPoint = GetPos(alarmUnit.targetHistory[index].box, alarmUnit.targetHistory[index].targetPos);
        auto lineDir   = currPoint - lastPoint;
        auto lineLength = util::Length(lineDir);
        if (lineLength == 0) {
            continue;
        }

        lines.push_back(std::make_pair(lastPoint, currPoint));
    }

    return lines;
}

std::vector<std::pair<util::Point, util::Point>> TaskAlarm::GetBoxLines(util::Box box, int width,
                                                                        int height) {
    return util::GetBoxOsdLines(box, width, height);
}

std::string TaskAlarm::RecordVideoJson(CMsgOnEventsReq& event) {
    MsgAlarmVideoOverviewInfo jsonFileInfo;
    jsonFileInfo.algorithmCode = GetAlgId();
    jsonFileInfo.area          = GetArea(event.areaId);

    std::string jsonStr;
    std::string fileName = cosmo::path::GetEventPath(event.itimestamp);

    fileName = (std::filesystem::path(fileName) / (event.messageId + ".json")).string();
    auto ret = util::EncodeJson(jsonFileInfo, jsonStr);
    if (ret) {
        ret = util::WriteFile(fileName, jsonStr);
        if (false == ret) {
            LOG_WARN("write record video file Failed {}", fileName);
            return "";
        }
    }
    return fileName;
}

}  // namespace cosmo
