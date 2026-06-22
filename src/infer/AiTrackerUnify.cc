// AiTrackerUnify — Ai Tracker Unify implementation.

#include "infer/AiTrackerUnify.h"

#include <algorithm>

#include "nn/utils/tracker_wrap.h"
#include "util/Log.h"
#include "util/UuidUtil.h"

namespace cosmo {
AiTrackerUnify::AiTrackerUnify(const std::string& atomic_code, std::vector<std::string> labels,
                               std::vector<AiConfidence> confidence, int max_width, int max_height)
    : atomic_code_(atomic_code),
      max_width_(max_width),
      max_height_(max_height),
      tracker_(std::make_shared<cosmo::nn::TrackerWrap>()) {
    if (tracker_) {
        cosmo::nn::Rect2f region(0.0f, 0.0f, static_cast<float>(max_width), static_cast<float>(max_height));
        auto ret = tracker_->SetRegion(region);
        LOG_INFO("Track SetRegion {}x{} Get:{}", max_width_, max_height_, ret.description());
        // Class ID starts from 1.
        int class_id_count = 1;
        for (auto& label_name : labels) {
            TrackLabel label_el;
            label_el.class_id = class_id_count++;
            label_el.label    = label_name;
            float thresh_low  = 0.01f;
            float thresh      = 0.03f;
            auto conf_it      = std::find_if(confidence.begin(), confidence.end(),
                                             [&label_name](const auto& c) { return c.label == label_name; });
            if (conf_it != confidence.end()) {
                thresh     = conf_it->confidence;
                thresh_low = thresh * 0.7f;
            }
            track_labels_.push_back(label_el);
            track_config_.thresh_low.push_back(thresh_low);
            track_config_.thresh.push_back(thresh);
            LOG_INFO("Track {} class_id:{} Set thresh_low:{} thresh:{} ", label_el.label, label_el.class_id,
                     thresh_low, thresh);
        }

        ret = tracker_->SetTrackerConfig(track_config_);
        LOG_INFO("Track SetTrackerConfig  Get:{}", ret.description());

        ret = tracker_->GetTrackerConfig(track_config_);
        LOG_INFO(
            "Track GetTrackerConfig  Get:{} thresh size:{} thresh_low size:{} classid_same:{} "
            "dynamic_match:{} max_age:{} max_range:{} measurement_noise:{} trajectory_length:{} min_hits:{} "
            "motion_iou:{} motion_length:{} vertex_out_of_region:{} process_noise:{} thresh_low_timeout:{}",
            ret.description(), track_config_.thresh.size(), track_config_.thresh_low.size(),
            track_config_.classid_same, track_config_.dynamic_match, track_config_.max_age,
            track_config_.max_range, track_config_.measurement_noise.size(), track_config_.trajectory_length,
            track_config_.min_hits, track_config_.motion_iou, track_config_.motion_length,
            track_config_.vertex_out_of_region, track_config_.process_noise,
            track_config_.thresh_low_timeout);
    }
    LOG_INFO("Track {}x{}", max_width_, max_height_);
}

AiTrackerUnify::~AiTrackerUnify() {}

util::ErrorEnum AiTrackerUnify::Trace(std::vector<AiDetectRstEl>& input, std::vector<AiDetectRstEl>& rst) {
    if (!tracker_) {
        return util::ErrorEnum::NotInit;
    }

    auto track_input = DetEl2TrackEl(input);
    std::vector<cosmo::nn::TrackingBox> track_output;

    auto ret = tracker_->Trace(track_input, track_output);
    if (0 != ret) {
        return util::ErrorEnum::AI_TRACK_FAILED;
    }

    rst = TrackEl2DetEl(track_output);

    TrackDataSignDetId(rst, input);

    return util::ErrorEnum::Success;
}

util::ErrorEnum AiTrackerUnify::SetConfidence(std::vector<AiConfidence> confidence) {
    if ((track_labels_.size() != track_config_.thresh_low.size()) ||
        (track_labels_.size() != track_config_.thresh.size())) {
        LOG_WARN("Tarck ParamSize UnEqual lableSize:{} threshLow:{} threshHigh:{}", track_labels_.size(),
                 track_config_.thresh_low.size(), track_config_.thresh.size());
        return util::ErrorEnum::AI_TRACK_PARAM_INVALID;
    }
    size_t index     = 0;
    bool have_change = false;
    for (auto& label_el : track_labels_) {
        auto conf_it = std::find_if(confidence.begin(), confidence.end(),
                                    [&label_el](const auto& c) { return c.label == label_el.label; });
        if (conf_it != confidence.end()) {
            have_change = true;
            LOG_INFO("Track {} class_id:{} thresh Set From {} To {} ", label_el.label, label_el.class_id,
                     conf_it->confidence, track_config_.thresh[index]);
            track_config_.thresh[index]     = conf_it->confidence;
            track_config_.thresh_low[index] = track_config_.thresh[index] * 0.7f;
        }
        index++;
    }
    if (!have_change) {
        return util::ErrorEnum::Success;
    }
    auto ret = tracker_->SetTrackerConfig(track_config_);
    if (0 != ret) {
        return util::ErrorEnum::AI_TRACK_FAILED;
    }
    return util::ErrorEnum::Success;
}

util::ErrorEnum AiTrackerUnify::SetConfidence(AiConfidence confidence) {
    if ((track_labels_.size() != track_config_.thresh_low.size()) ||
        (track_labels_.size() != track_config_.thresh.size())) {
        LOG_WARN("Tarck ParamSize UnEqual lableSize:{} threshLow:{} threshHigh:{}", track_labels_.size(),
                 track_config_.thresh_low.size(), track_config_.thresh.size());
        return util::ErrorEnum::AI_TRACK_PARAM_INVALID;
    }
    size_t index = 0;
    for (auto& label_el : track_labels_) {
        if (confidence.label == label_el.label) {
            LOG_INFO("Track {} class_id:{} thresh Set From {} To {} ", label_el.label, label_el.class_id,
                     confidence.confidence, track_config_.thresh[index]);
            track_config_.thresh[index]     = confidence.confidence;
            track_config_.thresh_low[index] = track_config_.thresh[index] * 0.7f;
            auto ret                        = tracker_->SetTrackerConfig(track_config_);
            if (0 != ret) {
                return util::ErrorEnum::AI_TRACK_FAILED;
            }
            break;
        }
        index++;
    }
    return util::ErrorEnum::Success;
}

util::ErrorEnum AiTrackerUnify::SetConfig(float motion_iou, int motion_length, double dynamic_match) {
    bool have_change = false;
    if ((motion_iou >= 0) && (track_config_.motion_iou != motion_iou)) {
        have_change              = true;
        track_config_.motion_iou = motion_iou;
    }

    if ((motion_length > 2) && (track_config_.motion_length != motion_length)) {
        have_change                 = true;
        track_config_.motion_length = motion_length;
    }

    if ((dynamic_match > 0) && (track_config_.dynamic_match != dynamic_match)) {
        have_change                 = true;
        track_config_.dynamic_match = dynamic_match;
    }
    if (!have_change) {
        return util::ErrorEnum::Success;
    }
    LOG_INFO("Track SetTrackerConfig  motion_iou:{} motion_length:{} dynamic_match:{}", motion_iou,
             motion_length, dynamic_match);
    auto ret = tracker_->SetTrackerConfig(track_config_);
    if (0 != ret) {
        return util::ErrorEnum::AI_TRACK_FAILED;
    }
    return util::ErrorEnum::Success;
}

std::vector<cosmo::nn::TrackingBox> AiTrackerUnify::DetEl2TrackEl(std::vector<AiDetectRstEl>& input) {
    std::vector<cosmo::nn::TrackingBox> tracking_boxs;
    for (auto& el : input) {
        cosmo::nn::TrackingBox track_box_el;
        auto iter_find = std::find_if(
            track_labels_.begin(), track_labels_.end(),
            [&](const TrackLabel& track_label_el) { return el.confidence.label == track_label_el.label; });
        if (iter_find != track_labels_.end()) {
            track_box_el.id           = -1;
            track_box_el.class_id     = iter_find->class_id;
            track_box_el.confidence   = el.confidence.confidence;
            track_box_el.box.x        = static_cast<float>(el.box.x);
            track_box_el.box.y        = static_cast<float>(el.box.y);
            track_box_el.box.width    = static_cast<float>(el.box.width);
            track_box_el.box.height   = static_cast<float>(el.box.height);
            track_box_el.status       = cosmo::nn::TrackingStatus::NEW;
            track_box_el.motion_state = cosmo::nn::MotionState::UNCERTAIN;
            tracking_boxs.push_back(track_box_el);
        }
    }

    return tracking_boxs;
}

std::vector<AiDetectRstEl> AiTrackerUnify::TrackEl2DetEl(std::vector<cosmo::nn::TrackingBox>& input) {
    std::vector<AiDetectRstEl> det_rsts;
    for (auto& el : input) {
        auto iter_find = std::find_if(
            track_labels_.begin(), track_labels_.end(),
            [&](const TrackLabel& track_label_el) { return el.class_id == track_label_el.class_id; });
        if (iter_find != track_labels_.end()) {
            AiDetectRstEl det_box_el;
            det_box_el.classId                = iter_find->class_id;
            det_box_el.box.x                  = static_cast<int>(el.box.x);
            det_box_el.box.y                  = static_cast<int>(el.box.y);
            det_box_el.box.width              = static_cast<int>(el.box.width);
            det_box_el.box.height             = static_cast<int>(el.box.height);
            det_box_el.confidence.confidence  = el.confidence;
            det_box_el.confidence.label       = iter_find->label;
            det_box_el.confidence.atomic_code = atomic_code_;
            det_box_el.trackId                = el.id;
            det_box_el.trackStatus            = AITrackStatusChange(el.status);
            det_box_el.motionStatus           = AIMotionStatusChange(el.motion_state);
            if (det_box_el.box.width > 0)
                det_box_el.hwRatio =
                    static_cast<float>(det_box_el.box.height) / static_cast<float>(det_box_el.box.width);
            det_rsts.push_back(det_box_el);
        }
    }

    return det_rsts;
}

void AiTrackerUnify::TrackDataSignDetId(std::vector<AiDetectRstEl>& track_out,
                                        const std::vector<AiDetectRstEl>& input) {
    for (auto& target : track_out) {
        bool find_it  = false;
        auto match_it = std::find_if(input.begin(), input.end(), [&target](const auto& track_in) {
            return (track_in.confidence.label == target.confidence.label) &&
                   (track_in.box.x == target.box.x) && (track_in.box.y == target.box.y) &&
                   (track_in.box.width == target.box.width) && (track_in.box.height == target.box.height);
        });
        if (match_it != input.end()) {
            target.targetId = match_it->targetId;
            find_it         = true;
        }
        if (!find_it) {
            target.targetId = util::GenerateUUID();
        }
    }
}

AITrackingStatus AiTrackerUnify::AITrackStatusChange(cosmo::nn::TrackingStatus status) {
    switch (status) {
        case cosmo::nn::TrackingStatus::NEW:
            return AITrackingStatus::NEW;
        case cosmo::nn::TrackingStatus::TRACKING:
            return AITrackingStatus::TRACKING;
        case cosmo::nn::TrackingStatus::LOSS:
            return AITrackingStatus::LOSS;
        default:
            return AITrackingStatus::UNKNOW;
    }

    return AITrackingStatus::UNKNOW;
}

AIMotionState AiTrackerUnify::AIMotionStatusChange(cosmo::nn::MotionState status) {
    switch (status) {
        case cosmo::nn::MotionState::UNCERTAIN:
            return AIMotionState::UNCERTAIN;
        case cosmo::nn::MotionState::MOVING:
            return AIMotionState::MOVING;
        case cosmo::nn::MotionState::STILL:
            return AIMotionState::STILL;
        default:
            return AIMotionState::UNCERTAIN;
    }

    return AIMotionState::UNCERTAIN;
}
}  // namespace cosmo