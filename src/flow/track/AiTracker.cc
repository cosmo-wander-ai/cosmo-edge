// AiTracker — Deformation filtering

#include "flow/track/AiTracker.h"

#include <algorithm>
#include <numeric>

#include "flow/common/AlgDataRecord.h"
#include "flow/track/AiTrackerTypes.h"
#include "util/GeometricCalculation.h"
#include "util/GeometricPos.h"
#include "util/Keys.h"
#include "util/LimitedType.h"
#include "util/Log.h"
#include "util/SafeParse.h"
#include "util/UuidUtil.h"

static constexpr const char* kTag = "AI-TRACKER ";
namespace cosmo {

AiTracker::~AiTracker() {
    LOG_INFO("{}[{} {}] Stop", kTag, name_, uuid);
    Stop();
    if (tracker_) {
        tracker_.reset();
        tracker_ = nullptr;
    }
    LOG_INFO("{}[{} {}] Delete", kTag, name_, uuid);
}

AiTracker::AiTracker(const std::string& taskId, ActionNode& action)
    : AlgActionBase(AlgActionType::AlgActionAiTracker, action, "", taskId),
      name_(taskId),
      overview_rec_inst_(taskId, "track") {
    uuid = util::GenerateUUID();
    data_queue->SetMaxSize(6);

    for (auto& actionParams : action.configObject.params) {
        if (key::MOTION_STATUS == actionParams.key.ToString()) {
            motion_status_ = static_cast<TrackMotionStatusFilter>(util::ParseInt(actionParams.value));
            LOG_INFO(
                "ModifyParam "
                "[{} {}] {}  Set To {}",
                name_, uuid, actionParams.key, actionParams.value);
        } else if (key::SHAPE_CHANGE_STATUS == actionParams.key.ToString()) {
            shape_change_filter_ = static_cast<TrackShapeChangeFilter>(util::ParseInt(actionParams.value));
            LOG_INFO(
                "ModifyParam "
                "[{} {}] {}  Set To {}",
                name_, uuid, actionParams.key, actionParams.value);
        } else if (key::SHAPE_CHANGE_THRESHOLD == actionParams.key.ToString()) {
            shape_change_threshold_ = util::ParseFloat(actionParams.value);
            LOG_INFO(
                "ModifyParam "
                "[{} {}] {}  Set To {}",
                name_, uuid, actionParams.key, actionParams.value);
        }
    }
    action_status = util::ErrorEnum::ActionReady;
    LOG_INFO("{}[{} {}] Init ", kTag, name_, uuid);
}

// Param/Confidence methods — moved to AiTrackerParam.cc

void AiTracker::SetAtomicCode(const std::string& detAtomicCode) {
    if (!atomic_code_.empty()) {
        return;
    }

    atomic_code_        = detAtomicCode;
    param_.atomic_code  = atomic_code_;
    bool findAtomicCode = false;
    // Transfer previously issued params_ to param_, param_ only has tracking parameters corresponding to
    // the detection algorithm
    auto it = std::find_if(params_.begin(), params_.end(),
                           [this](const auto& p) { return p.atomic_code == param_.atomic_code; });
    if (it != params_.end()) {
        param_.motion.motion              = it->motion.motion;
        param_.motion.frames              = it->motion.frames;
        param_.motion.track_dynamic_match = it->motion.track_dynamic_match;
        findAtomicCode                    = true;
    }
    // Atomic algorithm code not found, indicating parameters were not issued
    if (!findAtomicCode) {
        LOG_WARN("{}[{} {}] Have No Track Param. Detect atomicCode:{}", kTag, name_, uuid, atomic_code_);
    }
}

float AiTracker::CalculateNormalizedVariation(std::queue<float> ratios) {
    if (ratios.size() < 2) {
        return 0.0f;  // If data is empty, return 0
    }

    // Convert queue to vector
    std::vector<float> ratiosVector;
    while (!ratios.empty()) {
        ratiosVector.push_back(ratios.front());
        ratios.pop();
    }

    // Find maximum value
    float maxRatio = *std::max_element(ratiosVector.begin(), ratiosVector.end());
    if (maxRatio == 0) {
        return 0.0f;  // If maximum value is 0, return 0
    }
    // Normalize aspect ratio
    std::vector<float> normalizedRatios;
    std::transform(ratiosVector.begin(), ratiosVector.end(), std::back_inserter(normalizedRatios),
                   [maxRatio](float ratio) { return ratio / maxRatio; });
    // Calculate normalized average
    float sum  = std::accumulate(normalizedRatios.begin(), normalizedRatios.end(), 0.0f);
    float mean = sum / static_cast<float>(normalizedRatios.size());
    // Calculate normalized variance
    float variance =
        std::accumulate(normalizedRatios.begin(), normalizedRatios.end(), 0.0f,
                        [mean](float acc, float ratio) { return acc + std::pow(ratio - mean, 2.0f); });
    float stdDev = std::sqrt(variance / static_cast<float>(normalizedRatios.size()));
    // Return standard deviation
    return stdDev;
}

void AiTracker::Filter(DataDetTrackClassifyPtr data) {
    bool bMotionFilter = true;

    // Do not filter by motion status
    if (!((TrackMotionStatusFilter::kStatic == motion_status_) ||
          (TrackMotionStatusFilter::kMove == motion_status_))) {
        bMotionFilter = false;
    }

    AIMotionState stayStatus =
        (TrackMotionStatusFilter::kStatic == motion_status_) ? AIMotionState::MOVING : AIMotionState::STILL;
    for (auto& target : data->targets) {
        // Filter by tracking status
        if (AITrackingStatus::TRACKING != target.trackStatus) {
            target.bFilter    = true;
            target.filterDesc = "Filter By Tracking Status";
            target.filterType = AIFilterType::TrackingStatus;
            continue;
        }

        // Filter by motion status
        if ((bMotionFilter) && (stayStatus != target.motionStatus)) {
            target.bFilter    = true;
            target.filterDesc = "Filter By Track Motion Status";
            target.filterType = AIFilterType::MotionStatus;
            continue;
        }

        if (TrackShapeChangeFilter::kStatic == shape_change_filter_) {
            if (target.shapeChangeStatus == AIShapeChangeState::STILL) {
                target.bFilter    = true;
                target.filterDesc = "Filter By Track Shape Change STILL";
                target.filterType = AIFilterType::ShapeStatus;
                continue;
            }
        }

        if (TrackShapeChangeFilter::kMove == shape_change_filter_) {
            if (target.shapeChangeStatus == AIShapeChangeState::CHANGE) {
                target.bFilter    = true;
                target.filterDesc = "Filter By Track Shape Change";
                target.filterType = AIFilterType::ShapeStatus;
                continue;
            }
        }
    }
}

void AiTracker::HandFrame(AlgDataPtr algData) {
    debug_last_line_ = __LINE__;
    if (!algData) {
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }

    debug_last_line_ = __LINE__;
    SetAtomicCode(algData->chanDataDetect.atomicCode);
    debug_last_line_ = __LINE__;
    if (!tracker_) {
        int width  = algData->chanDataDec.frame ? static_cast<int>(algData->chanDataDec.frame->GetWidth())
                                                : media::kVideoDefaultWidth;
        int height = algData->chanDataDec.frame ? static_cast<int>(algData->chanDataDec.frame->GetHeight())
                                                : media::kVideoDefaultHeight;
        if (!AiSdkInit(algData->chanDataDetect.atomicCode, algData->chanDataDetect.lables, width, height)) {
            action_status = util::ErrorEnum::AI_INST_NOTCREATED;
            return;
        }
    }
    debug_last_line_ = __LINE__;
    // Decoded data
    if (AlgDataType::ChannelDataDetect != algData->dataType) {
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }
    debug_last_line_ = __LINE__;
    frame_index_     = algData->chanDataDec.frame ? algData->chanDataDec.frame->GetFrameIndex() : 0;
    if (last_frame_index_ > frame_index_) {
        LOG_WARN("{}[{} {}] DEBUG Last Frame:{} This Frame:{}", kTag, name_, uuid, last_frame_index_,
                 frame_index_);
    }
    last_frame_index_ = frame_index_;
    debug_last_line_  = __LINE__;
    // Tracking

    // Detection data is shared among multiple tasks in the channel, tracking needs to be copied out
    // AlgDataPtr algOutData = std::make_shared<AlgData>();
    AlgDataPtr algOutData      = AlgDataCopy(algData);
    algOutData->dataType       = AlgDataType::TaskDataTrack;
    algOutData->bHaveTrack     = true;
    algOutData->bHaveRelated   = algData->bHaveRelated;
    algOutData->channelId      = algData->channelId;
    algOutData->firstTimePoint = algData->firstTimePoint;
    algOutData->taskId         = name_;
    // algOutData->chanDataOrig.packet = algData->chanDataOrig.packet;
    // algOutData->chanDataDec.frame = algData->chanDataDec.frame;
    // algOutData->chanDataDec.reportTimeStamp = algData->chanDataDec.reportTimeStamp;
    // algOutData->chanDataDetect.detRet = algData->chanDataDetect.detRet;
    // algOutData->taskDataAlarm.alarmData = algData->taskDataAlarm.alarmData;
    auto trackRst         = std::make_shared<DataDetTrackClassify>();
    trackRst->streamIndex = algData->chanDataDetect.detRet->streamIndex;
    trackRst->frameIndex  = algData->chanDataDetect.detRet->frameIndex;
    trackRst->timestamp   = algData->chanDataDetect.detRet->timestamp;
    trackRst->picWidth    = algData->chanDataDetect.detRet->picWidth;
    trackRst->picHeight   = algData->chanDataDetect.detRet->picHeight;
    algOutData->SetTaskResult(AlgDataType::TaskDataTrack, trackRst);
    debug_last_line_ = __LINE__;
    if (param_modify_sign_ != param_active_sign_) {
        param_active_sign_ = param_modify_sign_;
        LOG_INFO("{}[{} {}] Param Changed", kTag, name_, uuid);
        tracker_->SetConfidence(GetConfidence());
        tracker_->SetConfig(param_.motion.motion, param_.motion.frames, param_.motion.track_dynamic_match);
    }
    debug_last_line_ = __LINE__;
    action_status    = tracker_->Trace(algData->chanDataDetect.detRet->targets, trackRst->targets);
    auto frameIndex =
        algOutData->chanDataDec.frame ? algOutData->chanDataDec.frame->GetFrameIndex() : uint64_t{0};
    auto streamIndex =
        algOutData->chanDataDec.frame ? algOutData->chanDataDec.frame->GetStreamIndex() : int64_t{0};
    for (auto& target : trackRst->targets) {
        target.frameIndex  = frameIndex;
        target.streamIndex = static_cast<size_t>(streamIndex);
    }
    debug_last_line_ = __LINE__;
    SignTargetAreas(algOutData);
    debug_last_line_ = __LINE__;
    // Calculate line crossing
    TargetAddHistory(algOutData);
    debug_last_line_ = __LINE__;
    TargetOldHistory(algOutData);
    debug_last_line_ = __LINE__;
    TargetCalcBreakLine(algOutData);
    debug_last_line_ = __LINE__;
    CpBreakLineStatusToData(algOutData);
    debug_last_line_ = __LINE__;
    // Filter based on motion_status_ flag
    Filter(trackRst);
    debug_last_line_ = __LINE__;
    //  	algData->chanDataDetect.lables.size(),algData->chanDataDetect.detRet->targets.size(),
    //  	algOutData->GetTaskResult(AlgDataType::TaskDataTrack)->targets.size());
    // Distribution queue
    debug_last_line_ = __LINE__;
    RecordHistory(algOutData);
    debug_last_line_ = __LINE__;
    distributor->DistributorData(algOutData);
    debug_last_line_ = __LINE__;
    return;
}

void AiTracker::RecordHistory(AlgDataPtr dataPtr) {
    if (!dataPtr)
        return;
    auto trackResult = dataPtr->GetTaskResult(AlgDataType::TaskDataTrack);
    if (!trackResult)
        return;

    std::lock_guard<std::shared_mutex> lock(mtx);
    historys_.push_back(*trackResult);
    if (trackResult->timestamp - historys_.front().timestamp > media::kVideoInfoMaxDuration) {
        historys_.pop_front();
    }

    overview_rec_inst_.OverviewRecordFrame(trackResult);
}

MsgOverviewMem AiTracker::GetOverviewInfo(const std::string& /*channelId*/, const std::string& /*taskId*/,
                                          int64_t streamIndex, int64_t from, int64_t to) {
    return overview_rec_inst_.GetOverviewInfo(streamIndex, from, to);
}

std::vector<DataDetTrackClassify> AiTracker::GetHistory(const std::string& /*channelId*/,
                                                        const std::string& /*taskId*/, int64_t from,
                                                        int64_t ts, int64_t to) {
    std::vector<DataDetTrackClassify> rst;
    std::shared_lock<std::shared_mutex> lock(mtx);
    bool bStart = false;
    for (auto historyEl : historys_) {
        auto timestampDiff = (ts == 0) ? 0 : abs(ts - historyEl.timestamp);
        if ((from <= historyEl.frameIndex) &&
            (timestampDiff < media::kTimestampDiff))  // Used for alarm acquisition, start frame is I frame
                                                      // and may not have it
        {
            bStart = true;
        }
        if (bStart) {
            historyEl.dataType = AlgDataType::TaskDataTrack;
            rst.push_back(historyEl);
            if (to <= historyEl.frameIndex)  // End frame may not have it
            {
                break;
            }
        }
    }

    return rst;
}

// Area/BreakLine methods — moved to AiTrackerArea.cc

}  // namespace cosmo
