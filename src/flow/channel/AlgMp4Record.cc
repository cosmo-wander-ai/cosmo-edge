// AlgMp4Record — MP4 recording task lifecycle, upload and JSON metadata.

#include "flow/channel/AlgMp4Record.h"

#include <filesystem>

#include "media/VideoUtil.h"
#include "mp4v2/mp4v2.h"
#include "mp4v2/track.h"
#include "service/detail/ServiceRegistry.h"
#include "service/path/IFileService.h"
#include "service/system/IConfigReadService.h"
#include "service/task/ITaskQuery.h"
#include "util/FileUtil.h"
#include "util/FormatString.h"
#include "util/JsonStructUtil.h"
#include "util/Log.h"
#include "util/PathUtil.h"

#define VALUE_SET_IN_RANGE(target, value, min, max)                                                          \
    if ((value >= min) && (value <= max))                                                                    \
        target = value;

namespace cosmo {
static constexpr int timeScale = 90000;
namespace fs                   = std::filesystem;

AlgMp4Record::AlgMp4Record(media::VideoCodecType streamType, const RecordParam& recordParam, float fps,
                           int width, int height) {
    track_id_  = MP4_INVALID_TRACK_ID;
    target_id_ = recordParam.targetId;
    VALUE_SET_IN_RANGE(fps_, fps, 0.1f, 120.0f);
    VALUE_SET_IN_RANGE(width_, width, 1, media::kVideoMaxWidth);
    VALUE_SET_IN_RANGE(height_, height, 1, media::kVideoMaxHeight);
    event_time_            = recordParam.frameTimestamp;
    event_index_           = recordParam.frameSeq;
    stream_index_          = recordParam.streamIndex;
    task_start_frame_seq_  = recordParam.startFrameSeq;
    task_start_frame_time_ = recordParam.startframeTimestamp;

    event_name_   = recordParam.recordId;
    retro_direct_ = recordParam.retroDirect;
    auto filePath = GetPath();
    file_name_    = (fs::path(filePath) / (event_name_ + "_video.tmp")).string();
    mp4_name_     = (fs::path(filePath) / (event_name_ + "_video.mp4")).string();

    max_frame_size_    = static_cast<size_t>(width) * static_cast<size_t>(height) * 3 / 2;
    task_id_           = recordParam.taskId;
    channel_id_        = recordParam.channelId;
    upload_url_        = recordParam.url;
    upload_json_url_   = recordParam.jsonUrl;
    json_path_         = recordParam.jsonPath;
    overview_json_url_ = recordParam.overviewUrl;

    mp4_handle_ = MP4Create(file_name_.c_str());
    if (!mp4_handle_) {
        LOG_ERRO("[MP4 TASK] {} {} Create {} Failed.", task_id_, event_name_, file_name_);
        return;
    }
    MP4SetVideoProfileLevel(mp4_handle_, 1);
    source_type_ = streamType;

    LOG_INFO(
        "[MP4 TASK] {} {} Create {} With StreamType:{} Fps:{} WxH:{}x{} eventTime:{} EventFrame:{} "
        "StartFrame:{} Ok.",
        task_id_, event_name_, file_name_, streamType, fps, width, height, event_time_, event_index_,
        task_start_frame_seq_);
}

AlgMp4Record::~AlgMp4Record() {
    if (mp4_handle_) {
        LOG_INFO(
            "[MP4 TASK] {} {} stop. Frame Start/Event/End at {}/{}/{} Duration Pre/After: {}/{} ms "
            "recordFrame:{} DataSize:{} WriteSize:{}",
            task_id_, event_name_, start_index_, event_index_, last_index_, (event_time_ - start_time_),
            (last_time_ - event_time_), record_frames_, data_size_, total_size_);
        MP4Close(mp4_handle_);
        if (record_frames_ > 0) {
            std::error_code err;
            fs::rename(file_name_, mp4_name_, err);
            std::string bucket = "gaf_commodity_video";
            // Upload to file server.
            std::string mp4Name = mp4_name_;

            if (service::ServiceRegistry::Instance().Get<service::IConfigReadService>().IsNetworkModel()) {
                LOG_INFO(" {} Wait To Upload To {}", mp4_name_, upload_url_);

                service::ServiceRegistry::Instance().Get<service::IFileService>().UploadFile(
                    event_name_,
                    [mp4Name](const std::string& taskId, bool bFinished, void* /*ptr*/) {
                        if (!bFinished) {
                            LOG_WARN("{} Upload {} Failed", taskId, mp4Name);
                        } else {
                            LOG_INFO("{} Upload {} Success!", taskId, mp4Name);
                        }

                        // Delete the file after upload.
                        remove(mp4Name.c_str());
                    },
                    nullptr, "mp4", mp4_name_, bucket, upload_url_);
            }

            UploadJsonFile();

            UploadOverviewFile();
        }
    } else {
        LOG_WARN(
            "[MP4 TASK] {} {} stop For Mp4 Create Failed. Frame Start/Event/End at {}/{}/{} Duration "
            "Pre/After: {}/{} ms",
            task_id_, event_name_, start_index_, event_index_, last_index_, (event_time_ - start_time_),
            (last_time_ - event_time_));
    }
}

void AlgMp4Record::UploadOverviewFile() {
    MsgAlarmVideoOverviewInfoExtra overviewInfo;
    overviewInfo.algorithmCode = overview_info_.algorithmCode;
    overviewInfo.area          = overview_info_.area;
    overviewInfo.infos =
        service::ServiceRegistry::Instance().Get<cosmo::service::ITaskQuery>().GetTaskLiveOverviewInfo(
            task_id_, stream_index_, task_start_frame_seq_, last_index_);

    std::string jsonStr;
    auto ret = util::EncodeJson(overviewInfo, jsonStr);
    if (ret) {
        std::string fileName = (fs::path(GetPath()) / (event_name_ + "_overview.json")).string();
        util::WriteFile(fileName, jsonStr);
        if (service::ServiceRegistry::Instance().Get<service::IConfigReadService>().IsNetworkModel()) {
            std::string bucket = "gaf_commodity_video";
            service::ServiceRegistry::Instance().Get<service::IFileService>().UploadFile(
                event_name_ + "_overview",
                [fileName](const std::string& taskId, bool bFinished, void* /*ptr*/) {
                    if (!bFinished) {
                        LOG_WARN("{} Upload {} Failed", taskId, fileName);
                    } else {
                        LOG_INFO("{} Upload {} Success!", taskId, fileName);
                    }

                    // Delete the file after upload.
                    remove(fileName.c_str());
                },
                nullptr, "json", fileName, bucket, overview_json_url_);
        }
    }
}

void AlgMp4Record::UploadJsonFile() {
    auto hisJson = util::ReadFile(json_path_);
    MsgAlarmVideoOverviewInfo jsonFileInfo;
    if (!util::DecodeJson(hisJson, jsonFileInfo)) {
        remove(json_path_.c_str());
        return;
    }
    overview_info_ = jsonFileInfo;
    MsgAlarmVideoOverviewFrame lastframe;
    bool bHaveLastFrame = false;
    bool bStart         = false;
    auto history = service::ServiceRegistry::Instance().Get<cosmo::service::ITaskQuery>().GetTaskDetHistory(
        channel_id_, task_id_, task_start_frame_seq_, task_start_frame_time_, last_index_);
    for (auto& el : history) {
        if (target_id_ < 0)  // No tracking.
        {
            if (AlgDataType::ChannelDataDetect != el.dataType) {
                continue;
            }
        } else {
            if (AlgDataType::TaskDataTrack != el.dataType) {
                continue;
            }
        }

        // Locate the start position.
        if ((el.frameIndex < task_start_frame_seq_)) {
            if (bStart) {
                LOG_WARN("[MP4 TASK] {} {} Maybe Video Repeat, EventStart Frame:{} RecordFrame:{}", task_id_,
                         event_name_, task_start_frame_seq_, el.frameIndex);
                break;
            }
            continue;
        }

        if (!bStart) {
            bStart        = true;
            auto addEmpty = el.frameIndex - task_start_frame_seq_;

            while (addEmpty-- > 0) {
                MsgAlarmVideoOverviewFrame head;
                head.index = el.frameIndex - task_start_frame_seq_ - addEmpty;
                head.color = 1;
                jsonFileInfo.targets.push_back(head);
            }
        }

        if ((el.frameIndex > last_index_)) {
            LOG_INFO("[MP4 TASK] {} {} Stop, EventEnd Frame:{} RecordFrame:{}", task_id_, event_name_,
                     last_index_, el.frameIndex);
            break;
        }

        MsgAlarmVideoOverviewFrame frame;
        frame.index = el.frameIndex - task_start_frame_seq_;
        frame.color = 1;
        if ((el.picWidth <= 0) || (el.picHeight <= 0)) {
            LOG_WARN("[MP4 TASK] {} {} From {} To {} Now:{}", task_id_, event_name_, task_start_frame_seq_,
                     last_index_, el.frameIndex);
            continue;
        }

        for (auto target : el.targets) {
            if ((target_id_ >= 0))  // Only keep the alarm target when tracking is active.
            {
                if (target.trackId != target_id_) {
                    continue;
                }
            }
            MsgRect msgTarget;
            msgTarget.x      = static_cast<double>(target.box.x) / el.picWidth;
            msgTarget.y      = static_cast<double>(target.box.y) / el.picHeight;
            msgTarget.width  = static_cast<double>(target.box.width) / el.picWidth;
            msgTarget.height = static_cast<double>(target.box.height) / el.picHeight;

            msgTarget.x = std::min(msgTarget.x, 1.0);
            msgTarget.x = std::max(msgTarget.x, 0.0);

            msgTarget.y = std::min(msgTarget.y, 1.0);
            msgTarget.y = std::max(msgTarget.y, 0.0);

            msgTarget.width = std::min(msgTarget.width, 1.0);
            msgTarget.width = std::max(msgTarget.width, 0.0);

            msgTarget.height = std::min(msgTarget.height, 1.0);
            msgTarget.height = std::max(msgTarget.height, 0.0);

            frame.rects.push_back(msgTarget);
        }
        if (bHaveLastFrame) {
            int iLastCount = 0;
            while (frame.index != lastframe.index + 1) {
                lastframe.index += 1;
                jsonFileInfo.targets.push_back(lastframe);
                iLastCount++;
                if (iLastCount > 30) {
                    LOG_WARN("[MP4 TASK] {} {} From {} To {} New:{} Last:{}", task_id_, event_name_,
                             task_start_frame_seq_, last_index_, frame.index, lastframe.index);
                    break;
                }
            }
        }

        lastframe      = frame;
        bHaveLastFrame = true;
        jsonFileInfo.targets.push_back(frame);
    }

    std::string jsonStr;
    jsonFileInfo.area.iretroDirect = retro_direct_;
    jsonFileInfo.area.retroDirect  = std::to_string(static_cast<int>(retro_direct_));
    auto ret                       = util::EncodeJson(jsonFileInfo, jsonStr);
    if (ret) {
        util::WriteFile(json_path_, jsonStr);
    }

    if (service::ServiceRegistry::Instance().Get<service::IConfigReadService>().IsNetworkModel()) {
        std::string jsonName = json_path_;
        std::string bucket   = "gaf_commodity_video";
        service::ServiceRegistry::Instance().Get<service::IFileService>().UploadFile(
            event_name_,
            [jsonName](const std::string& taskId, bool bFinished, void* /*ptr*/) {
                if (!bFinished) {
                    LOG_WARN("{} Upload {} Failed", taskId, jsonName);
                } else {
                    LOG_INFO("{} Upload {} Success!", taskId, jsonName);
                }

                // Delete the file after upload.
                remove(jsonName.c_str());
            },
            nullptr, "json", jsonName, bucket, upload_json_url_);
    }
}

}  // namespace cosmo
