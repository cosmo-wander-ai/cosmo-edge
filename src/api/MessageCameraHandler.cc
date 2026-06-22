// MessageCameraHandler — Message Camera Handler implementation.

#include "api/MessageCameraHandler.h"

#include <filesystem>

#include "service/camera/ICameraChannelQuery.h"
#include "service/camera/ICameraDeviceCrud.h"
#include "service/camera/ICameraTaskConfig.h"
#include "service/task/ITaskQuery.h"
#include "util/ErrorCode.h"
#include "util/FileUtil.h"
#include "util/Log.h"
#include "util/TimeUtil.h"
#include "util/UuidUtil.h"

namespace cosmo {

namespace {
    static constexpr const char* kTag = "MessageCameraHandler";

    // Video file size constraints for AddVideo validation.
    constexpr size_t kMinVideoFileSize = 1 * 1024 * 1024;     // 1 MB
    constexpr size_t kMaxVideoFileSize = 1024 * 1024 * 1024;  // 1 GB
}  // namespace

MessageCameraHandler::MessageCameraHandler(service::ICameraDeviceCrud& crud,
                                           service::ICameraChannelQuery& query,
                                           service::ICameraTaskConfig& task_cfg,
                                           service::ITaskQuery& task_query)
    : crud_(crud), query_(query), task_cfg_(task_cfg), task_query_(task_query) {}

camera::MsgAddSend MessageCameraHandler::Handle(camera::MsgAddRecv&& data, std::error_condition& errc) {
    camera::MsgAddSend retData{};
    errc = crud_.Add(data, retData.resData.id);

    return retData;
}

camera::MsgAddVideoSend MessageCameraHandler::Handle(camera::MsgAddVideoRecv&& data,
                                                     std::error_condition& errc) {
    camera::MsgAddVideoSend retData{};
    MsgCameraInfo info;
    info.url         = data.filePath;
    info.channelCode = data.channelCode;
    info.channelName = data.channelName;
    info.channelType = MsgCameraType::MsgCameraTypeLocalVideo;

    // P0-1: Guard against URLs without a file extension.
    auto dot_pos = info.url.find_last_of('.');
    if (dot_pos == std::string::npos) {
        errc = util::ErrorEnum::FileNotSupport;
        return retData;
    }
    std::string videoType = info.url.substr(dot_pos);
    if (videoType != ".mp4" && videoType != ".mkv") {
        LOG_INFO("videoType:{}", videoType);
        errc = util::ErrorEnum::FileNotSupport;
        return retData;
    }

    // P0-2: Guard against invalid contentLength strings.
    size_t contentLength = 0;
    try {
        contentLength = std::stoul(data.contentLength);
    } catch (const std::exception&) {
        errc = util::ErrorEnum::ParameterException;
        return retData;
    }
    if (contentLength < kMinVideoFileSize) {
        errc = util::ErrorEnum::FileSizeSmall;
        return retData;
    }
    if (contentLength > kMaxVideoFileSize) {
        errc = util::ErrorEnum::FileSizeBig;
        return retData;
    }

    errc = crud_.Add(info, retData.resData.id);

    return retData;
}

camera::MsgUpdateSend MessageCameraHandler::Handle(camera::MsgUpdateRecv&& data, std::error_condition& errc) {
    camera::MsgUpdateSend retData{};
    errc = crud_.Update(data);

    return retData;
}

camera::MsgPageSend MessageCameraHandler::Handle(camera::MsgPageRecv&& data, std::error_condition& /*errc*/) {
    camera::MsgPageSend retData{};
    if (query_.IsIotNetworkMode()) {
        std::vector<MsgCameraInfo> cameraInfos = task_query_.CameraTaskInfo();
        size_t indexStart                      = (data.pageNum - 1) * data.pageSize;
        size_t indexEnd                        = data.pageNum * data.pageSize;
        retData.resData.total                  = cameraInfos.size();
        for (size_t index = 0; index < cameraInfos.size(); index++) {
            if ((index >= indexStart) && (index < indexEnd)) {
                task_query_.GetChannelAttr(cameraInfos[index].videoChannelId, cameraInfos[index]);
                retData.resData.rows.push_back(cameraInfos[index]);
            }
        }
    } else {
        retData.resData.rows = crud_.Query(data.channelName, static_cast<int>(data.channelStatus),
                                           data.pageNum, data.pageSize, retData.resData.total);
    }

    return retData;
}

camera::MsgDeleteSend MessageCameraHandler::Handle(camera::MsgDeleteRecv&& data, std::error_condition& errc) {
    camera::MsgDeleteSend retData{};
    errc = crud_.Delete(data.videoChannelId);
    return retData;
}

camera::MsgBatchDeleteSend MessageCameraHandler::Handle(camera::MsgBatchDeleteRecv&& data,
                                                        std::error_condition& /*errc*/) {
    camera::MsgBatchDeleteSend retData{};
    for (auto videoChannelId : data.videoChannelIds) {
        std::error_condition ret = crud_.Delete(videoChannelId);
        if (util::ErrorEnum::Success != ret) {
            MsgResultInfo failedEl;
            failedEl.id      = videoChannelId;
            failedEl.resCode = ret.value();
            failedEl.resMsg  = ret.message();
            retData.resData.failedList.push_back(failedEl);
        }
    }

    return retData;
}

// ── GetPicture helper methods ──────────────────────────────────────────────

std::string MessageCameraHandler::CaptureLiveFrame(const std::string& channel_id,
                                                   std::error_condition& errc) {
    auto picture = task_cfg_.CaptureImage(channel_id);
    if (!picture) {
        return {};
    }

    auto pictureJpeg = query_.EncodeJpeg(picture);
    if (pictureJpeg.empty()) {
        errc = util::ErrorEnum::ImageEncodeFailed;
        return {};
    }

    auto timestamp = util::GetMilliseconds();
    auto path      = query_.GetWebLocalPath(timestamp);
    auto fileName  = util::GenerateUUID() + ".jpg";
    auto fullName  = (std::filesystem::path(path) / fileName).string();
    auto ret       = util::WriteFile(fullName, reinterpret_cast<const std::uint8_t*>(pictureJpeg.data()),
                                     static_cast<int>(pictureJpeg.size()));
    if (!ret) {
        LOG_WARN("write image file Failed {}", fullName);
        errc = util::ErrorEnum::ImageEncodeFailed;
        return {};
    }

    // Also save a channel-named cache copy for fallback.
    SaveChannelCache(channel_id, pictureJpeg);

    return (std::filesystem::path(query_.GetWebAccessPath(timestamp)) / fileName).string();
}

void MessageCameraHandler::SaveChannelCache(const std::string& channel_id, const std::vector<uint8_t>& jpeg) {
    auto channelFileName = channel_id + ".jpg";
    auto channelFilePath = (std::filesystem::path(query_.GetWebLocalPath()) / channelFileName).string();
    util::WriteFile(channelFilePath, reinterpret_cast<const std::uint8_t*>(jpeg.data()),
                    static_cast<int>(jpeg.size()));
}

std::string MessageCameraHandler::FallbackToCache(const std::string& channel_id, std::error_condition& errc) {
    auto channelFileName = channel_id + ".jpg";
    auto channelFilePath = (std::filesystem::path(query_.GetWebLocalPath()) / channelFileName).string();
    if (util::FileExist(channelFilePath)) {
        return (std::filesystem::path(query_.GetWebAccessPath()) / channelFileName).string();
    }
    errc = util::ErrorEnum::ImageCatchFailed;
    return {};
}

camera::MsgGetPictureSend MessageCameraHandler::Handle(camera::MsgGetPictureRecv&& data,
                                                       std::error_condition& errc) {
    camera::MsgGetPictureSend retData{};

    // Always try to get real-time frame via CaptureImage (supports on-demand channel cold start)
    auto url = CaptureLiveFrame(data.videoChannelId, errc);
    if (!url.empty()) {
        retData.resData.url = std::move(url);
    } else if (errc == util::ErrorEnum::Success) {
        // CaptureImage failed (e.g. timeout), fallback to local cached screenshot
        retData.resData.url = FallbackToCache(data.videoChannelId, errc);
    }

    return retData;
}

camera::MsgQueryUsbCameraListSend MessageCameraHandler::Handle(camera::MsgQueryUsbCameraListRecv&& data,
                                                               std::error_condition& errc) {
    (void)data;
    camera::MsgQueryUsbCameraListSend retData{};
    retData.resData.rows = query_.QueryUsbCameraList();
    errc                 = util::ErrorEnum::Success;
    return retData;
}
}  // namespace cosmo