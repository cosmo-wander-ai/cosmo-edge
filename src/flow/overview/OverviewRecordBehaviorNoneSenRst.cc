// OverviewRecordBehaviorNoneSenRst — Overview Record Behavior None Sen Rst implementation.

#include "flow/overview/OverviewRecordBehaviorNoneSenRst.h"

#include <filesystem>

#include "service/detail/ServiceRegistry.h"
#include "service/system/IOverviewConfig.h"
#include "util/FileUtil.h"
#include "util/JsonStructUtil.h"
#include "util/PathUtil.h"

namespace cosmo {

OverviewRecordBehaviorNoneSenRst::OverviewRecordBehaviorNoneSenRst(const std::string& task_id,
                                                                   const std::string& name,
                                                                   size_t limit_count,
                                                                   size_t limit_duration_ms)
    : task_id_(task_id),
      name_(name),
      limit_count_(limit_count),
      limit_duration_ms_(limit_duration_ms),
      is_overview_enabled_(false) {}

void OverviewRecordBehaviorNoneSenRst::OverviewRecordFrame(const MsgRecPosSaveSensitity& rec_data) {
    // Whether to save structured file
    if (!service::ServiceRegistry::Instance().Get<service::IOverviewConfig>().GetOverviewStructureRecord()) {
        is_overview_enabled_ = false;
        return;
    }

    bool need_clear = false;
    // The switch was turned off in the middle
    if (!is_overview_enabled_) {
        need_clear = true;
        // Local file create directory
        auto path        = cosmo::path::GetTaskOverviewDataPath(task_id_);
        local_file_name_ = (std::filesystem::path(path) / (name_ + ".json")).string();
    }

    SaveMem(rec_data, need_clear);
    SaveLocalFile(rec_data, need_clear);

    is_overview_enabled_ = true;
}

// Interface to get information, mark after each fetch, do not fetch repeatedly
MsgOverviewMem OverviewRecordBehaviorNoneSenRst::GetOverviewInfoMsg() {
    MsgOverviewMem info;
    for (auto& frame : frames_) {
        if (!frame.bMsgReq) {
            MsgRecPosSaveSensitity node = frame;
            info.posSaveSens.push_back(node);
            frame.bMsgReq = true;
        }
    }

    return info;
}

// Interface to get information, mark after each fetch, do not fetch repeatedly
MsgOverviewMem OverviewRecordBehaviorNoneSenRst::GetOverviewInfoDuration(int64_t stream_index, int64_t from,
                                                                         int64_t to) {
    MsgOverviewMem info;
    for (const auto& frame : frames_) {
        if ((frame.streamIndex == stream_index) && (frame.index >= from) && (frame.index <= to)) {
            MsgRecPosSaveSensitity node = frame;
            // Frame starts from offset position, used for video recording
            node.index = frame.index - from;
            info.posSaveSens.push_back(node);
        }
    }

    return info;
}

MsgOverviewMem OverviewRecordBehaviorNoneSenRst::GetOverviewInfo(int64_t stream_index, int64_t from,
                                                                 int64_t to) {
    MsgOverviewMem info;
    // Whether to save structured file
    if (!service::ServiceRegistry::Instance().Get<service::IOverviewConfig>().GetOverviewStructureRecord()) {
        is_overview_enabled_ = false;
        return info;
    }

    std::lock_guard<std::shared_mutex> lock(mtx_);
    if (frames_.empty()) {
        return info;
    }
    if ((stream_index == -1) && (from == -1) && (to == -1)) {
        info = GetOverviewInfoMsg();
    } else {
        info = GetOverviewInfoDuration(stream_index, from, to);
    }

    info.name = name_;
    info.type = MsgOverviewMemDataType::MsgOverviewMemDataTypePosSaveSensitity;

    return info;
}

void OverviewRecordBehaviorNoneSenRst::SaveLocalFile(const MsgRecPosSaveSensitity& rec_data,
                                                     bool need_clear) const {
    if (!service::ServiceRegistry::Instance().Get<service::IOverviewConfig>().GetOverviewStructureFile()) {
        return;
    }
    std::string rec_str;
    if (!util::EncodeJson(rec_data, rec_str, -1, ' ', 4)) {
        return;
    }
    rec_str.append("\n");

    if (need_clear) {
        util::WriteFile(local_file_name_, rec_str);
    } else {
        util::WriteFileAppend(local_file_name_, rec_str);
    }
}

void OverviewRecordBehaviorNoneSenRst::SaveMem(const MsgRecPosSaveSensitity& rec_data, bool need_clear) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    if (need_clear) {
        frames_.clear();
    }

    frames_.push_back(rec_data);
    while (frames_.size() > 1) {
        auto last_timestamp  = frames_.back().timestamp;
        auto front_timestamp = frames_.front().timestamp;
        if ((last_timestamp < front_timestamp) ||
            (static_cast<size_t>(last_timestamp - front_timestamp) > limit_duration_ms_)) {
            frames_.pop_front();
        } else if (frames_.size() > limit_count_) {
            frames_.pop_front();
        } else {
            break;
        }
    }
}

}  // namespace cosmo