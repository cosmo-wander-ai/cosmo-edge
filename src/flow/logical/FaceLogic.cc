// FaceLogic — Face Logic implementation.

#include "flow/logical/FaceLogic.h"

#include <algorithm>
#include <chrono>
#include <thread>

#include "flow/logical/FaceLogicTypes.h"
#include "service/detail/ServiceRegistry.h"
#include "util/Keys.h"
#include "util/Log.h"
#include "util/SafeParse.h"

static constexpr const char *kTag = "CaptureLogic ";
namespace cosmo {

// TrackIdData — defined in FaceLogicTypes.h

FaceLogic::~FaceLogic() {
    LOG_INFO("{}Task:{} Stop", kTag, task_id);
    Stop();
    LOG_INFO("{}Task:{} Delete", kTag, task_id);
}

FaceLogic::FaceLogic(const std::string &taskId, ActionNode &action)
    : AlgActionBase(AlgActionType::AlgActionBAFaceLogic, action, "", taskId) {
    action_status = util::ErrorEnum::ActionReady;
    for (auto &el : action.configObject.params) {
        if (key::capture::IN_OUT_IMG == el.key.ToString()) {
            int value              = util::ParseInt(el.value);
            params_.in_out_picture = value ? true : false;
            LOG_INFO("{}Task:{} Init {} Set To {}", kTag, task_id, el.key, el.value);
        }
    }

    LOG_INFO("{}Task:{} Init", kTag, task_id);
}

void FaceLogic::QueueStatus(std::vector<AlgActionDataQueueStatus> &queStatus, unsigned int durationSec) {
    AlgActionDataQueueStatus status;
    if (data_queue->Status(status.queueStatus, durationSec)) {
        status.actionId = GetActionId();
        status.taskIds.push_back(task_id);
        status.actionStatus = action_status;

        queStatus.push_back(status);
    }
}

/*
param.capture_strategy
param.realtime_duration
param.capture_quality
param.capture_angles
*/
bool FaceLogic::AnalysisKey(MsgDynamicKeyValue &param) {
    if (param.keys.empty()) {
        LOG_WARN(
            "ModifyParam "
            "[{}] param.keys is Empty",
            task_id);
        return false;
    }
    if (param.keys.size() != 2) {
        LOG_DEBUG(
            "ModifyParam "
            "[{}] Set {} Failed. key size:{}",
            task_id, param.key, param.keys.size());
        return false;
    }

    if (key::PARAM != param.keys[0]) {
        LOG_DEBUG(
            "ModifyParam "
            "[{}] param.keys[0] is Not {}",
            task_id, key::PARAM);
        return false;
    }

    if (param.keys[1] == key::capture::STRATEGY) {
        auto value = util::ParseInt(param.value);
        if (!ValidateCaptureStrategy(value)) {
            LOG_WARN(
                "ModifyParam "
                "[{}] param.key {} value {} is Illegal",
                task_id, param.key, param.value);
            return false;
        }
        params_.capture_strategy = static_cast<FaceLogicCaptureStrategy>(value);
        LOG_INFO(
            "ModifyParam "
            "[{}] Set {} To {}. srcValue Is {}",
            task_id, param.keys[1], value, param.value);
    } else if (param.keys[1] == key::capture::REALTIME_DURATION) {
        auto value = util::ParseInt(param.value);
        if (!ValidateRealtimeDuration(value)) {
            LOG_WARN(
                "ModifyParam "
                "[{}] param.key {} value {} is Illegal",
                task_id, param.key, param.value);
            return false;
        }
        params_.realtime_duration = value;
        LOG_INFO(
            "ModifyParam "
            "[{}] Set {} To {}. srcValue Is {}",
            task_id, param.keys[1], value, param.value);
    } else if (param.keys[1] == key::capture::QUALITY) {
        auto value = util::ParseFloat(param.value);
        if (!ValidateQuality(value)) {
            LOG_WARN(
                "ModifyParam "
                "[{}] param.key {} value {} is Illegal",
                task_id, param.key, param.value);
            return false;
        }
        params_.capture_quality = value;
        LOG_INFO(
            "ModifyParam "
            "[{}] Set {} To {}. srcValue Is {}",
            task_id, param.keys[1], value, param.value);
    } else if (param.keys[1] == key::capture::ANGLES) {
        auto keys = util::Split(param.value.ToRefString(), ",");
        params_.capture_angles.clear();
        for (auto &key : keys) {
            auto value = util::ParseInt(key.data());
            if (!ValidateFaceAngle(value)) {
                LOG_WARN(
                    "ModifyParam "
                    "[{}] param.key {} value {}/{} is Illegal",
                    task_id, param.key, param.value, value);
                return false;
            }
            params_.capture_angles.push_back(static_cast<FaceAngle>(value));
            LOG_INFO(
                "ModifyParam "
                "[{}] Set {} To {}. srcValue Is {} capture_angles Size:{}",
                task_id, param.keys[1], value, param.value, params_.capture_angles.size());
        }

    } else {
        return false;
    }

    return true;
}

// Modify parameters based on existing ones
bool FaceLogic::ModifyParam(const std::string & /*channelId*/, const std::string & /*taskId*/,
                            std::vector<MsgDynamicKeyValue> &params) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    for (auto &param : params) {
        AnalysisKey(param);
    }

    return false;
}

// Set parameters - clear previous ones and set fully new ones
bool FaceLogic::SetParam(const std::string & /*channelId*/, const std::string & /*taskId*/,
                         std::vector<MsgDynamicKeyValue> &params) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    // Clear parameters first
    params_ = {};
    for (auto &param : params) {
        AnalysisKey(param);
    }

    return false;
}

bool FaceLogic::FindAngleInConfig(FaceAngle &angle) {
    std::vector<FaceAngle> configAngles;
    {
        std::lock_guard<std::shared_mutex> lock(mtx);
        configAngles = params_.capture_angles;
    }

    return std::any_of(configAngles.begin(), configAngles.end(),
                       [&angle](const auto &cfgAngle) { return cfgAngle == angle; });
}

void FaceLogic::RecordIntoArea(TrackIdData &track_id_data, AiDetectRstEl &target, VideoFramePtr frame) {
    if (det_data_status_.bHaveArea) {
        for (auto &area : target.areaSign.areas) {
            auto &intoAreaPic = track_id_data.into_area_pics[area.area_id];
            if (!intoAreaPic.is_init) {
                intoAreaPic.is_init   = true;
                intoAreaPic.area_name = area.area_name;
                if (params_.in_out_picture) {
                    intoAreaPic.pic_frame = frame;
                }

                intoAreaPic.pic_info = target;
                LOG_INFO("{}[{}] track_id:{} AREA-ACTION Into Area:{}/{}", kTag, task_id, target.trackId,
                         area.area_id, area.area_name);
            }
        }
    } else {
        auto &intoAreaPic = track_id_data.into_area_pics[std::string(key::DEFAULT_AREA)];
        if (!intoAreaPic.is_init) {
            intoAreaPic.is_init   = true;
            intoAreaPic.area_name = std::string(key::DEFAULT_AREA_NAME);
            if (params_.in_out_picture) {
                intoAreaPic.pic_frame = frame;
            }
            intoAreaPic.pic_info = target;
            LOG_INFO("{}[{}] track_id:{} AREA-ACTION Into Area:{}/{}", kTag, task_id, target.trackId,
                     std::string(key::DEFAULT_AREA), intoAreaPic.area_name);
        }
    }
}

void FaceLogic::HandOutArea(AlgDataPtr dataPtr, TrackIdData &track_id_data, const AiDetectRstEl &target,
                            VideoFramePtr /*frame*/) {
    for (auto &lastArea : track_id_data.det_rst.areaSign.areas) {
        if (!AreaInAreas(target.areaSign.areas, lastArea)) {
            LOG_INFO("{}[{}] track_id:{} AREA-ACTION Out Area:{}/{}", kTag, task_id, track_id_data.track_id,
                     lastArea.area_id, lastArea.area_name);
            HandBestFace(dataPtr, track_id_data, lastArea.area_id, lastArea.area_name);
            track_id_data.into_area_pics.erase(lastArea.area_id);
        }
    }
}

void FaceLogic::HandOutAreaWithDisapear(AlgDataPtr dataPtr, TrackIdData &track_id_data,
                                        VideoFramePtr /*frame*/) {
    for (auto &lastArea : track_id_data.into_area_pics) {
        if (lastArea.second.is_init) {
            HandBestFace(dataPtr, track_id_data, lastArea.first, lastArea.second.area_name);
            LOG_INFO("{}[{}] track_id:{} AREA-ACTION Out Area:{}/{} MaxQualitity:{}", kTag, task_id,
                     track_id_data.track_id, lastArea.first, lastArea.second.area_name,
                     track_id_data.best_pic_max_quality);
        }
    }
}

// face matching — moved to FaceLogicMatch.cc

}  // namespace cosmo
