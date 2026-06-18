// Target selection optimization
// For faces: find the best face quality
// For bodies: find the best body quality
// For others: find the maximum size

#include "flow/target/TargetChooseBest.h"

#include "util/Keys.h"
#include "util/Log.h"
#include "util/TimeUtil.h"

static constexpr const char* kTag       = "TARGET-CHOOSE-BEST ";
static constexpr int kFilterLogInterval = 100;
namespace cosmo {

TargetChooseBest::~TargetChooseBest() {
    LOG_INFO("{}[{} {}] Stop", kTag, GetTaskId(), GetFlowActionId());
    Stop();
    LOG_INFO("{}[{} {}] Delete", kTag, GetTaskId(), GetFlowActionId());
}

TargetChooseBest::TargetChooseBest(const std::string& taskId, ActionNode& action)
    : AlgActionBase(AlgActionType::AlgActionBAAssoTarget, action, "", taskId),
      overview_rec_inst_(taskId, "targetChooseBest_" + action.flowActionId) {
    action_status = util::ErrorEnum::ActionReady;

    LOG_INFO("{}[{} {}] Init ", kTag, GetTaskId(), GetFlowActionId());
}

/*
Dynamic parameter key format:
aiParam.#{labelCode}.confidence
*/
bool TargetChooseBest::ValidKey(const MsgDynamicKeyValue& param) const {
    if (param.keys.empty()) {
        LOG_WARN(
            "ModifyParam "
            "[{} {}] param.keys is Empty",
            GetTaskId(), GetFlowActionId());
        return false;
    }
    if (param.keys.size() != 3) {
        LOG_DEBUG(
            "ModifyParam "
            "[{} {}] Set {} Failed. key size:{}",
            GetTaskId(), GetFlowActionId(), param.key, param.keys.size());
        return false;
    }

    if (key::AI_PARAM != param.keys[0]) {
        LOG_DEBUG(
            "ModifyParam "
            "[{} {}] param.keys[0]:{} is Not {}",
            GetTaskId(), GetFlowActionId(), param.keys[0], key::AI_PARAM);
        return false;
    }

    return true;
}

bool TargetChooseBest::AnalysisKey(const MsgDynamicKeyValue& param) const {
    if (!ValidKey(param)) {
        return false;
    }

    return false;
}

// Modify parameters based on existing ones
bool TargetChooseBest::ModifyParam(const std::string& /*channelId*/, const std::string& /*taskId*/,
                                   std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    for (auto& param : params) {
        AnalysisKey(param);
    }
    return true;
}

// Set parameters - clear previous ones and set fully new ones
bool TargetChooseBest::SetParam(const std::string& /*channelId*/, const std::string& /*taskId*/,
                                std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    // Clear parameters first
    for (auto& param : params) {
        AnalysisKey(param);
    }
    return true;
}

bool TargetChooseBest::GetQualityAngle(const AiDetectRstEl& target, float& quality, FaceAngle& angle,
                                       bool has_related) {
    AiConfidence face_quality;
    AiConfidence face_angle;
    std::vector<AiConfidence> classify_rst = target.classifyRst;
    util::Box box                          = target.box;
    if (has_related) {
        classify_rst = target.relatedEl.classifyRst;
        box          = target.relatedEl.box;
    }
    // Face quality
    if (GetFaceQuality(classify_rst, face_quality)) {
        GetFaceAngle(classify_rst, face_angle);
        GetFaceAngleType(face_angle, angle);
    }
    // If face quality not detected, detect body quality (body snapshot logic)
    else if (GetPedQuality(classify_rst, face_quality)) {
        angle = FaceAngle::FaceAngleFront;
    }
    // For license plate recognition, use size as quality score
    else {
        angle                   = FaceAngle::FaceAngleFront;
        face_quality.confidence = box.width * box.height;
    }

    quality = face_quality.confidence;
    return true;
}

struct TargetChooseBest::TrackIdData {
    int track_id{-1};
    std::string track_id_info;
    bool detected{false};
    float best_quality{-1.0};
    AiDetectBestEl best_el;
};

void TargetChooseBest::ChooseBest(VideoFramePtr frame, DataDetTrackClassifyPtr input, bool has_related) {
    if (!VideoFrameValid(frame)) {
        action_status = util::ErrorEnum::InvalidImage;
        return;
    }
    if (!input) {
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }
    for (auto& target : input->targets) {
        auto& id_data    = track_id_status_[target.trackId];
        id_data.track_id = target.trackId;
        id_data.detected = true;
        if (target.bFilter) {
            continue;
        }

        float quality   = -1.0;
        FaceAngle angle = FaceAngle::FaceAngleMax;
        GetQualityAngle(target, quality, angle, has_related);
        if ((quality > id_data.best_quality) && (FaceAngle::FaceAngleFront == angle)) {
            id_data.best_quality = quality;

            target.bestEl.bestQuality = quality;
            target.bestEl.bActive     = true;
            target.bestEl.bestFrame   = frame;
            if (has_related) {
                target.bestEl.box = target.relatedEl.box;
            } else {
                target.bestEl.box = target.box;
            }
            id_data.best_el = target.bestEl;
        } else {
            target.bestEl = id_data.best_el;
        }
    }

    for (auto it = track_id_status_.begin(); it != track_id_status_.end();) {
        if (!it->second.detected) {
            it = track_id_status_.erase(it);
        } else {
            it->second.detected = false;
            ++it;
        }
    }

    action_status = util::ErrorEnum::Success;
}

void TargetChooseBest::HandFrame(AlgDataPtr dataPtr) {
    AlgDataPtr algData = AlgDataCopy(dataPtr);
    if (!algData) {
        invalid_frame_cnt += 1;
        if (0 == invalid_frame_cnt % kFilterLogInterval) {
            LOG_WARN("{}[{} {}] Filter {} Frames", kTag, GetTaskId(), GetFlowActionId(), invalid_frame_cnt);
        }
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }

    if (!algData->chanDataDec.frame) {
        action_status = util::ErrorEnum::InvalidImage;
        return;
    }
    frame_index_  = algData->chanDataDec.frame->GetFrameIndex();
    stream_index_ = algData->chanDataDec.frame->GetStreamIndex();
    timestamp_    = algData->chanDataDec.frame->GetTimestamp();
    width_        = algData->chanDataDec.frame->GetWidth();
    height_       = algData->chanDataDec.frame->GetHeight();

    if (!((AlgDataType::ChannelDataDetect == algData->dataType) ||
          (AlgDataType::TaskDataTrack == algData->dataType) ||
          (AlgDataType::TaskDataClassify == algData->dataType))) {
        invalid_frame_cnt += 1;
        if (0 == invalid_frame_cnt % kFilterLogInterval) {
            LOG_WARN("{}[{} {}] Filter {} Frames dataType:{}", kTag, GetTaskId(), GetFlowActionId(),
                     invalid_frame_cnt, algData->dataType);
        }
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }

    auto input = algData->GetTaskResult(AlgDataType::TaskDataClassify);
    if (AlgDataType::TaskDataTrack == algData->dataType) {
        input = algData->GetTaskResult(AlgDataType::TaskDataTrack);
    } else if (AlgDataType::ChannelDataDetect == algData->dataType) {
        input = algData->chanDataDetect.detRet;
    }

    ChooseBest(algData->chanDataDec.frame, input, algData->bHaveRelated);

    distributor->DistributorData(algData);
}

MsgOverviewMem TargetChooseBest::GetOverviewInfo(const std::string& /*channelId*/,
                                                 const std::string& /*taskId*/, int64_t streamIndex,
                                                 int64_t from, int64_t to) {
    return overview_rec_inst_.GetOverviewInfo(streamIndex, from, to);
}
}  // namespace cosmo
