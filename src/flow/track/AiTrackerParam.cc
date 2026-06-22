// AiTrackerParam.cc — Parameter and confidence management for AiTracker.
// Split from AiTracker.cc to reduce file size (DEBT-007).

#include <algorithm>

#include "flow/track/AiTracker.h"
#include "util/Keys.h"
#include "util/Log.h"
#include "util/SafeParse.h"
#include "util/dto/ActionCodes.h"

static constexpr const char* kTag = "AI-TRACKER ";
namespace cosmo {

std::vector<AiConfidence> AiTracker::GetConfidence() {
    std::vector<AiConfidence> confidences;
    std::shared_lock<std::shared_mutex> lock(mtx);
    for (auto& labelParam : label_param_) {
        AiConfidence confidence;
        confidence.label       = labelParam.label;
        confidence.confidence  = labelParam.confidence;
        confidence.atomic_code = atomic_code_;
        confidences.push_back(confidence);
    }

    return confidences;
}

bool AiTracker::AiSdkInit(const std::string& atomicCode, const std::vector<std::string>& labels, int maxWidth,
                          int maxHeight) {
    // 1. Error code 2. SDK initialization
    if (tracker_) {
        return true;
    }

    if ((labels.empty()) || (maxWidth <= 0) || (maxHeight <= 0)) {
        action_status = util::ErrorEnum::AI_INIT_PARAMERR;
        LOG_WARN("{}[{} {}] Sdk Init Failed, Illedge Param: lables:{} width:{} height:{}", kTag, name_, uuid,
                 labels.size(), maxWidth, maxHeight);
        return false;
    }

    tracker_ = std::make_shared<AiTrackerUnify>(atomicCode, labels, GetConfidence(), maxWidth, maxHeight);
    LOG_INFO("{}[{} {}] Init Sdk targetCount:{} {}x{}", kTag, name_, uuid, labels.size(), maxWidth,
             maxHeight);
    action_status = util::ErrorEnum::AI_INST_CREATED;
    tracker_->SetConfig(param_.motion.motion, param_.motion.frames, param_.motion.track_dynamic_match);
    return true;
}

bool AiTracker::ModifyTrackParamToLocal(AiTrackParam& trackParam, AiTrackParam& localTrackParam) {
    if (trackParam.motion.frames >= 0) {
        LOG_INFO(
            "ModifyParam "
            "atomicCode:{} Track Frame Modify From {} To {}",
            localTrackParam.atomic_code, localTrackParam.motion.frames, trackParam.motion.frames);
        localTrackParam.motion.frames = trackParam.motion.frames;
        return true;
    }

    if (trackParam.motion.track_dynamic_match >= 0) {
        LOG_INFO(
            "ModifyParam "
            "atomicCode:{} Track trackDynamicMatch Modify From {} To {}",
            localTrackParam.atomic_code, localTrackParam.motion.track_dynamic_match,
            trackParam.motion.track_dynamic_match);
        localTrackParam.motion.track_dynamic_match = trackParam.motion.track_dynamic_match;
        return true;
    }

    if (trackParam.motion.motion >= 0) {
        LOG_INFO(
            "ModifyParam "
            "atomicCode:{} Track Static Threhold Modify From {} To {}",
            localTrackParam.atomic_code, localTrackParam.motion.motion, trackParam.motion.motion);
        localTrackParam.motion.motion = trackParam.motion.motion;
        return true;
    }

    return false;
}

bool AiTracker::SetTrackParamToLocal(AiTrackParam& trackParam) {
    param_.atomic_code = atomic_code_;
    if (atomic_code_ == trackParam.atomic_code)
        ModifyTrackParamToLocal(trackParam, param_);
    auto it = std::find_if(params_.begin(), params_.end(),
                           [&trackParam](const auto& p) { return trackParam.atomic_code == p.atomic_code; });
    if (it != params_.end()) {
        return ModifyTrackParamToLocal(trackParam, *it);
    }

    if (trackParam.motion.frames >= 0) {
        LOG_INFO(
            "ModifyParam "
            "atomicCode:{} Track Frame Set To {}",
            trackParam.atomic_code, trackParam.motion.frames);
    }

    if (trackParam.motion.track_dynamic_match >= 0) {
        LOG_INFO(
            "ModifyParam "
            "atomicCode:{} Track trackDynamicMatch Set To {}",
            trackParam.atomic_code, trackParam.motion.track_dynamic_match);
    }

    if (trackParam.motion.motion >= 0) {
        LOG_INFO(
            "ModifyParam "
            "atomicCode:{} Track Static Threhold Set To {}",
            trackParam.atomic_code, trackParam.motion.motion);
    }
    params_.push_back(trackParam);
    return true;
}

bool AiTracker::SetConfidenceToLocal(AiConfidence& confidence) {
    auto it = std::find_if(label_param_.begin(), label_param_.end(),
                           [&confidence](const auto& p) { return confidence.label == p.label; });
    if (it != label_param_.end()) {
        LOG_INFO(
            "ModifyParam "
            " Track Confidence {} Modify From {} To {}",
            it->label, it->confidence, confidence.confidence);
        it->confidence = confidence.confidence;
        return true;
    }

    LOG_INFO(
        "ModifyParam "
        " Track Set {} Confidence To {}",
        confidence.label, confidence.confidence);
    AiLabelParam labelParam;
    labelParam.label      = confidence.label;
    labelParam.confidence = confidence.confidence;
    label_param_.push_back(labelParam);
    return true;
}

bool AiTracker::SetTargetPosToLocal(const std::string& inLabel, TargetPosition pos) {
    auto it = std::find_if(label_param_.begin(), label_param_.end(),
                           [&inLabel](const auto& p) { return inLabel == p.label; });
    if (it != label_param_.end()) {
        LOG_INFO(
            "ModifyParam "
            " Track TargetPosition {} Modify From {} To {}",
            it->label, it->pos, pos);
        it->pos = pos;
        return true;
    }

    LOG_INFO(
        "ModifyParam "
        " Track Set {} TargetPosition To {} WARNING Need Confidence",
        inLabel, pos);

    AiLabelParam labelParam;
    labelParam.label = inLabel;
    labelParam.pos   = pos;
    label_param_.push_back(labelParam);
    return true;
}

/*
Dynamic parameter key format:
aiParam.#{atomicCode}.motion
aiParam.#{atomicCode}.frames
aiParam.#{atomicCode}.trackDynamicMatch
aiParam.#{labelCode}.confidence
*/
bool AiTracker::ValidKey(const MsgDynamicKeyValue& param) {
    if (param.keys.empty()) {
        LOG_WARN(
            "ModifyParam "
            "[{} {}] param.keys is Empty",
            name_, uuid);
        return false;
    }
    if (param.keys.size() != 3) {
        LOG_DEBUG(
            "ModifyParam "
            "[{} {}] Set {} Failed. key size:{}",
            name_, uuid, param.key, param.keys.size());
        return false;
    }

    if (key::AI_PARAM != param.keys[0]) {
        LOG_DEBUG(
            "ModifyParam "
            "[{} {}] param.keys[0]:{} is Not {}",
            name_, uuid, param.keys[0], key::AI_PARAM);
        return false;
    }

    return true;
}

bool AiTracker::AnalysisKey(MsgDynamicKeyValue& param) {
    if (!ValidKey(param)) {
        return false;
    }

    if (param.keys[2] == key::CONFIDENCE) {
        AiConfidence confidence;
        confidence.label      = param.keys[1];
        confidence.confidence = util::ParseFloat(param.value);
        SetConfidenceToLocal(confidence);
    } else if (param.keys[2] == key::DET_POSITION) {
        TargetPosition value = static_cast<TargetPosition>(util::ParseInt(param.value));
        return SetTargetPosToLocal(param.keys[1], value);
    } else if (param.keys[2] == key::MOTION) {
        AiTrackParam trackParam;
        trackParam.atomic_code   = param.keys[1];
        trackParam.motion.motion = util::ParseFloat(param.value) / 100.0f;
        SetTrackParamToLocal(trackParam);
    } else if (param.keys[2] == key::FRAMES) {
        AiTrackParam trackParam;
        trackParam.atomic_code   = param.keys[1];
        trackParam.motion.frames = util::ParseInt(param.value);
        SetTrackParamToLocal(trackParam);
    } else if (param.keys[2] == key::DYNAMIC_MATCH) {
        AiTrackParam trackParam;
        trackParam.atomic_code                = param.keys[1];
        trackParam.motion.track_dynamic_match = util::ParseFloat(param.value);
        SetTrackParamToLocal(trackParam);
    }

    else {
        LOG_DEBUG(
            "ModifyParam "
            "[{} {}] param.key {} value {} is Unknow",
            name_, uuid, param.key, param.value);
        return false;
    }

    return true;
}

// Modify parameters - modify based on existing parameters
bool AiTracker::ModifyParam(const std::string& /*channelId*/, const std::string& /*taskId*/,
                            std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    for (auto& param : params) {
        AnalysisKey(param);
    }
    param_modify_sign_ += 1;
    return false;
}

// Set parameters - clear previous parameters and set entirely new ones
bool AiTracker::SetParam(const std::string& /*channelId*/, const std::string& /*taskId*/,
                         std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    // Clear parameters first
    param_ = {};
    params_.clear();
    label_param_.clear();
    for (auto& param : params) {
        AnalysisKey(param);
    }
    param_modify_sign_ += 1;
    return true;
}

// Set parameters - clear previous parameters and set entirely new ones
bool AiTracker::SetArea(const std::string& /*channelId*/, const std::string& taskId,
                        std::vector<MsgTaskArea>& areas, std::vector<MsgTaskArea>& shieldedAreas) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    task_area_.taskId        = taskId;
    task_area_.areas         = areas;
    task_area_.shieldedAreas = shieldedAreas;
    area_have_line_          = false;
    for (auto& area : task_area_.areas) {
        if (area.linePoints.size() > 1) {
            area_have_line_ = true;
        }
        area.iderectionType = GetDirectionTypeFromMsg(area.params);
        for (auto& assArea : area.associatedAreas) {
            assArea.iderectionType = GetDirectionTypeFromMsg(assArea.params);
        }
    }
    return true;
}

// Start getting detAtomicCode after detection data arrives

}  // namespace cosmo
