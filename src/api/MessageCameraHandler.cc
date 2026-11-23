// MessageCameraHandler — Message Camera Handler implementation.

#include "api/MessageCameraHandler.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <utility>

#include "api/HttpUploadClaim.h"
#include "service/camera/ICameraChannelQuery.h"
#include "service/camera/ICameraDeviceCrud.h"
#include "service/camera/ICameraTaskConfig.h"
#include "service/detail/ServiceRegistry.h"
#include "service/path/IUploadStagingService.h"
#include "service/task/ITaskQuery.h"
#include "util/CipherUtil.h"
#include "util/ErrorCode.h"
#include "util/FileUtil.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/TimeUtil.h"
#include "util/UuidUtil.h"

namespace cosmo {

namespace {
    static constexpr const char* kTag = "MessageCameraHandler";
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
    if (data.channelCodeConflict) {
        errc = util::ErrorEnum::ParameterException;
        return retData;
    }
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
    // Compare case-insensitively so .AVI/.MP4 etc. are accepted regardless of casing.
    std::string videoType = info.url.substr(dot_pos);
    std::transform(videoType.begin(), videoType.end(), videoType.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (videoType != ".mp4" && videoType != ".mkv" && videoType != ".avi" && videoType != ".dav") {
        LOG_INFO("videoType:{}", videoType);
        errc = util::ErrorEnum::FileNotSupport;
        return retData;
    }

    // P0-2: Guard against invalid contentLength strings.
    std::uint64_t content_length = 0;
    try {
        std::size_t parsed = 0;
        content_length     = std::stoull(data.contentLength, &parsed);
        if (parsed != data.contentLength.size()) {
            throw std::invalid_argument("trailing content length data");
        }
    } catch (const std::exception&) {
        errc = util::ErrorEnum::ParameterException;
        return retData;
    }
    if (content_length == 0) {
        errc = util::ErrorEnum::FileSizeSmall;
        return retData;
    }

    errc = crud_.Add(info, retData.resData.id);

    return retData;
}

camera::MsgAddVideoSend MessageCameraHandler::Handle(camera::MsgAddVideoRecv&& data,
                                                     const RequestDispatchContext& context,
                                                     std::error_condition& errc) {
    camera::MsgAddVideoSend result{};
    if (context.transport != RequestTransport::kHttp || context.principal.empty() ||
        data.channelCodeConflict) {
        errc = util::ErrorEnum::InvalidParam;
        return result;
    }

    service::StagedFileLease lease;
    if (!data.uploadId.empty()) {
        errc = service::ServiceRegistry::Instance().Get<service::IUploadStagingService>().Consume(
            context.principal, data.uploadId, service::UploadPurpose::kVideo, lease);
    } else {
        errc = detail::ClaimHttpUpload(context, data.filePath, service::UploadPurpose::kVideo, lease);
    }
    if (errc) {
        return result;
    }
    if (!lease.Revalidate()) {
        errc = util::ErrorEnum::FileAnalysisFailed;
        return result;
    }
    data.filePath      = lease.Path();
    data.contentLength = std::to_string(lease.Size());
    return Handle(std::move(data), errc);
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

std::string MessageCameraHandler::CaptureLiveFrame(const std::string& channel_id, std::error_condition& errc,
                                                   std::vector<uint8_t>* jpeg) {
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
    if (jpeg)
        *jpeg = std::move(pictureJpeg);

    return (std::filesystem::path(query_.GetWebAccessPath(timestamp)) / fileName).string();
}

void MessageCameraHandler::SaveChannelCache(const std::string& channel_id, const std::vector<uint8_t>& jpeg) {
    if (!path::IsSafePathComponent(channel_id))
        return;
    auto channelFileName = channel_id + ".jpg";
    auto channelFilePath = (std::filesystem::path(query_.GetWebLocalPath()) / channelFileName).string();
    util::WriteFile(channelFilePath, reinterpret_cast<const std::uint8_t*>(jpeg.data()),
                    static_cast<int>(jpeg.size()));
}

std::string MessageCameraHandler::FallbackToCache(const std::string& channel_id, std::error_condition& errc) {
    if (!path::IsSafePathComponent(channel_id)) {
        errc = util::ErrorEnum::ImageCatchFailed;
        return {};
    }
    auto channelFileName = channel_id + ".jpg";
    auto channelFilePath = (std::filesystem::path(query_.GetWebLocalPath()) / channelFileName).string();
    if (util::FileExist(channelFilePath)) {
        return (std::filesystem::path(query_.GetWebAccessPath()) / channelFileName).string();
    }
    errc = util::ErrorEnum::ImageCatchFailed;
    return {};
}

std::vector<uint8_t> MessageCameraHandler::ReadChannelCache(const std::string& channel_id) {
    if (!path::IsSafePathComponent(channel_id))
        return {};
    const auto dir   = query_.GetWebLocalPath();
    const int dir_fd = open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (dir_fd < 0)
        return {};
    const auto name = channel_id + ".jpg";
    const int fd    = openat(dir_fd, name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    close(dir_fd);
    if (fd < 0)
        return {};
    struct stat info {};
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size <= 0 ||
        info.st_size > 8 * 1024 * 1024) {
        close(fd);
        return {};
    }
    std::vector<uint8_t> jpeg(static_cast<size_t>(info.st_size));
    size_t done = 0;
    while (done < jpeg.size()) {
        const auto count = read(fd, jpeg.data() + done, jpeg.size() - done);
        if (count <= 0) {
            jpeg.clear();
            break;
        }
        done += static_cast<size_t>(count);
    }
    close(fd);
    return jpeg;
}

camera::MsgGetPictureSend MessageCameraHandler::GetPicture(const std::string& channel_id, bool include_image,
                                                           std::error_condition& errc) {
    camera::MsgGetPictureSend retData{};
    std::vector<uint8_t> jpeg;

    // Always try to get real-time frame via CaptureImage (supports on-demand channel cold start)
    auto url = CaptureLiveFrame(channel_id, errc, include_image ? &jpeg : nullptr);
    if (!url.empty()) {
        retData.resData.url = std::move(url);
    } else if (errc == util::ErrorEnum::Success) {
        // CaptureImage failed (e.g. timeout), fallback to local cached screenshot
        retData.resData.url = FallbackToCache(channel_id, errc);
        if (include_image && !retData.resData.url.empty())
            jpeg = ReadChannelCache(channel_id);
    }

    if (!jpeg.empty())
        retData.resData.dataUrl = "data:image/jpeg;base64," + util::EncBase64Ex(jpeg.data(), jpeg.size());

    return retData;
}

camera::MsgGetPictureSend MessageCameraHandler::Handle(camera::MsgGetPictureRecv&& data,
                                                       std::error_condition& errc) {
    return GetPicture(data.videoChannelId, false, errc);
}

camera::MsgGetPictureSend MessageCameraHandler::Handle(camera::MsgGetPictureRecv&& data,
                                                       const RequestDispatchContext& context,
                                                       std::error_condition& errc) {
    return GetPicture(data.videoChannelId,
                      context.transport == RequestTransport::kHttp && !context.principal.empty(), errc);
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
