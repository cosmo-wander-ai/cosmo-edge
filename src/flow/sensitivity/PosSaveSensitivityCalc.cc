// PosSaveSensitivityCalc.cc — sensitivity calculation.

#include "flow/common/AlgDataRecord.h"
#include "flow/sensitivity/PosSaveSensitivity.h"
#include "flow/sensitivity/PosSaveSensitivityTypes.h"
#include "util/Log.h"
#include "util/TimeUtil.h"
#include "util/UuidUtil.h"

static constexpr const char* kTag = "PosSaveSensitivity ";

namespace cosmo {

void PosSaveSensitivity::CalcSensitity(AlgDataPtr algData) {
    MsgRecPosSaveSensitity recSensitity;
    recSensitity.index       = frame_index_;
    recSensitity.streamIndex = stream_index_;
    recSensitity.timestamp   = timestamp_;
    auto paramRatio          = params_.sensitivity_ratio;
    for (auto it = map_track_id_status_.begin(); it != map_track_id_status_.end(); ++it) {
        TrackIdData& idData = it->second;
        MsgRecPosSaveSensitityTarget recTarget;
        TrackData2RecData(idData, recTarget);
        size_t totalCount = 0;
        size_t detCount   = 0;
        size_t logicCount = 0;
        // Record results first, structured files are also recorded here
        for (auto& el : idData.history) {
            if (el.logic_result) {
                detCount += 1;
                recTarget.rsts.push_back(true);
            } else {
                recTarget.rsts.push_back(false);
            }
            // Filtered ones do not participate in calculation
            if (!el.is_filtered) {
                logicCount += 1;
            }
        }

        if (idData.history.empty()) {
            // Structured file
            recSensitity.targets.push_back(recTarget);
            continue;
        }

        // Requirements already met, no need to calculate again (e.g., alcohol detection passed)
        if (idData.is_behavior_detected) {
            // Structured file
            recSensitity.targets.push_back(recTarget);
            continue;
        }

        totalCount = params_.pos_sen_total_count;
        // Parameter setting error
        if (totalCount == 0) {
            LOG_WARN("{}[{}] Param posSenTotalCount:{} Limit", kTag, task_id, params_.pos_sen_total_count);
            return;
        }
        float ratio = static_cast<float>(detCount) / totalCount;
        // The number of logical operations must reach the sensitivity calculation numerator
        if (logicCount >= params_.pos_sen_hit_count) {
            idData.logic_count_can_be_alarm = true;
            LOG_INFO("{}[{}] TrackId:{} [Behavior LogicCountFull] logicCount:{} posSenHitCount:{}", kTag,
                     task_id, idData.track_id, logicCount, params_.pos_sen_hit_count);
        }

        if ((detCount >= params_.pos_sen_hit_count)) {
            idData.is_behavior_detected = true;
            // Structured file record
            recTarget.behaviorDetected = true;
            idData.frame               = nullptr;
            // idData.history.clear();
            // idData.historyFull = false;

            LOG_INFO(
                "{}[{}] TrackId:{} [Behavior PASS] Detect:{} Total:{} queSize:{} At Frame:{} ratio:{} "
                "paramRatio:{}",
                kTag, task_id, idData.track_id, detCount, totalCount, idData.history.size(), frame_index_,
                ratio, paramRatio);
        } else {
            // Frame information
            if ((idData.logic_count_can_be_alarm) && (!idData.target.bFilter)) {
                if ((nullptr == idData.frame) && dec_frame_) {
                    LOG_INFO("{}DEBUG [{}] TrackId:{} Frame:{} DataFrame:{} srcFrame:{} [{}.{} {}x{}]", kTag,
                             task_id, idData.track_id, frame_index_,
                             algData->chanDataDec.frame->GetFrameIndex(), dec_frame_->GetFrameIndex(),
                             idData.target.box.x, idData.target.box.y, idData.target.box.width,
                             idData.target.box.height);
                    idData.frame                            = dec_frame_;
                    idData.target_confidence_info.targetPos = idData.target.targetPos;
                    idData.target_confidence_info.box       = idData.target.box;
                }
            }
        }
        recSensitity.targets.push_back(recTarget);

        // Reuse HandTrackAlarm logic here, real-time settlement
        if (params_.pos_sen_real_time_enable) {
            HandTrackAlarm(algData, idData, "CalcSen");
        }
    }

    overview_rec_inst_.OverviewRecordFrame(recSensitity);
}

void PosSaveSensitivity::FillAlarmData(AlgDataPtr algData, DataAlarmUnit& alarmUnit) {
    if (!algData->taskDataAlarm.alarmData) {
        algData->taskDataAlarm.alarmData = std::make_shared<DataAlarm>();
    }

    auto alarmData = algData->taskDataAlarm.alarmData;
    if (!alarmData) {
        LOG_WARN("{}[{}] NEED ALARM But No MEM", kTag, task_id);
        return;
    }

    // Only one alarm per frame, otherwise frame data will change
    if (!alarmData->alarms.empty()) {
        LOG_WARN("{}[{}] trackId:{} NEED ALARM But No Frame Maybe Change", kTag, task_id, alarmUnit.trackId);
        return;
    }
    should_alarm_ = true;
    alarmData->alarms.push_back(alarmUnit);
}

void PosSaveSensitivity::FillAlarmDataTrackId(DataAlarmUnit& alarmUnit, TrackIdData& idData) {
    // alarmTarget.box = // Max Confidence Box and Pic
    for (auto& trackData : idData.history) {
        DataAlarmTargetConfidence targetConf;
        targetConf.box       = trackData.target_confidence_info.box;
        targetConf.targetPos = trackData.target_confidence_info.targetPos;
        alarmUnit.targetHistory.push_back(targetConf);
    }
    alarmUnit.box = idData.target_confidence_info.box;
    if (alarmUnit.friends.empty()) {
        alarmUnit.boxs.push_back(alarmUnit.box);
    } else {
        alarmUnit.boxs = alarmUnit.friends;
    }

    for (auto& assoTarget : idData.group_targets) {
        alarmUnit.boxs.push_back(assoTarget.box);
    }
    alarmUnit.trackId    = idData.track_id;
    alarmUnit.strTrackId = idData.track_id_uuid;
    alarmUnit.matchInfo  = idData.target.matchInfo;
}

void PosSaveSensitivity::HandTrackAlarm(AlgDataPtr algData, TrackIdData& idData, const std::string& tag) {
    // Do not alarm if behavior has appeared
    if (idData.is_behavior_detected) {
        return;
    }
    auto duration = idData.last_timestamp - idData.first_timestamp;

    // Insufficient appearance time
    if (duration < params_.pos_sen_duration) {
        return;
    }
    if (!idData.logic_count_can_be_alarm) {
        return;
    }
    LOG_INFO("{}[{}] TrackId {} {} At Frame:{}. NEED ALARM From Areas:{} duration:{} ", kTag, task_id,
             idData.track_id, tag, frame_index_, idData.target.areaSign.areas.size(), duration);
    DataAlarmUnit alarmUnit;
    for (auto& area : idData.target.areaSign.areas) {
        alarmUnit.flowActionId = action_info_.flowActionId;
        alarmUnit.reportType   = OnEventsReportType::Trigger;
        alarmUnit.areaId       = area.area_id;
        alarmUnit.areaName     = area.area_name;
        FillAlarmDataTrackId(alarmUnit, idData);
        if (idData.frame) {
            // Only one alarm per frame, otherwise frame data is incorrect
            algData->chanDataDec.frame = idData.frame;
        }
        FillAlarmData(algData, alarmUnit);

        idData.history.clear();
        idData.logic_count_can_be_alarm = false;

        LOG_INFO("{}[{}] TrackId:{} {} [NEED ALARM] At Frame:{}. In Area:{}/{} groupTargets:{} DataFrame:{}",
                 kTag, task_id, idData.track_id, tag, frame_index_, area.area_id, area.area_name,
                 idData.group_targets.size(), algData->chanDataDec.frame->GetFrameIndex());
    }
}

void PosSaveSensitivity::OldTrackId(AlgDataPtr algData) {
    for (auto it = map_track_id_status_.begin(); it != map_track_id_status_.end();) {
        if (!it->second.is_track_exist) {
            LOG_AITARGET("{}[{}] TrackId {} Disappear", kTag, task_id, it->second.track_id);
            HandTrackAlarm(algData, it->second, "Disappear");
            it = map_track_id_status_.erase(it);
        } else {
            it->second.is_track_exist = false;
            ++it;
        }
    }
}

void PosSaveSensitivity::HandTrackData(AlgDataPtr algData, DataDetTrackClassifyPtr input) {
    AddHistory(algData, input);
    OldHistory();
    CalcSensitity(algData);

    OldTrackId(algData);
}

void PosSaveSensitivity::HandFrame(AlgDataPtr dataPtr) {
    AlgDataPtr algData = AlgDataCopy(dataPtr);
    should_alarm_      = false;
    if (!algData) {
        invalid_frame_cnt += 1;
        if (0 == invalid_frame_cnt % 100) {
            LOG_WARN("{}[{}] Filter {} Frames", kTag, task_id, invalid_frame_cnt);
        }
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }

    // Must have tracking
    if (!(algData->bHaveTrack)) {
        invalid_frame_cnt += 1;
        if (0 == invalid_frame_cnt % 100) {
            LOG_WARN("{}[{}] Filter {} Frames dataType:{}", kTag, task_id, invalid_frame_cnt,
                     algData->dataType);
        }
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }
    stream_index_ = algData->chanDataDec.frame ? algData->chanDataDec.frame->GetStreamIndex() : 0;
    frame_index_  = algData->chanDataDec.frame ? algData->chanDataDec.frame->GetFrameIndex() : 0;
    timestamp_    = algData->chanDataDec.frame ? algData->chanDataDec.frame->GetTimestamp() : 0;
    width_        = algData->chanDataDec.frame ? algData->chanDataDec.frame->GetWidth() : width_;
    height_       = algData->chanDataDec.frame ? algData->chanDataDec.frame->GetHeight() : height_;
    dec_frame_    = algData->chanDataDec.frame;
    // m_origFps = algData->chanDataOrig.fps;

    // Sensitivity of detection, tracking, classification
    if ((AlgDataType::TaskDataTrack == algData->dataType) ||
        (AlgDataType::TaskDataClassify == algData->dataType) ||
        (AlgDataType::TaskDataGroupClassify == algData->dataType)) {
        DataDetTrackClassifyPtr input;
        if (AlgDataType::TaskDataTrack == algData->dataType) {
            input = algData->GetTaskResult(AlgDataType::TaskDataTrack);
        } else {
            input = algData->GetTaskResult(AlgDataType::TaskDataClassify);
        }

        if (algData->bHaveTrack != has_track_) {
            if (handle_frame_cnt > 0)
                LOG_WARN("{}[{}] WorkFlow Change To {}", kTag, task_id,
                         algData->bHaveTrack ? "Have Track" : "No Track");
            has_track_ = algData->bHaveTrack;
            // Track from none to some, need to clear map_track_id_status_
            // Track from some to none, need to clear m_mapAreaTargetStatus
        }
        if (algData->bHaveClassify != has_classify_) {
            if (handle_frame_cnt > 0)
                LOG_WARN("{}[{}] WorkFlow Change To {}", kTag, task_id,
                         algData->bHaveClassify ? "Have Classify" : "No Classify");
            has_classify_ = algData->bHaveClassify;
        }

        if (has_track_) {
            HandTrackData(algData, input);
        }
    } else {
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }

    handle_frame_cnt += 1;
    if (0 == handle_frame_cnt % 100) {
        LOG_INFO("{}[{}] Handle {} Frames", kTag, task_id, handle_frame_cnt);
    }
    action_status = util::ErrorEnum::Success;
    // Push to subsequent (alarm) module when alarm is generated
    if ((algData->taskDataAlarm.alarmData) && (should_alarm_)) {
        algData->taskDataAlarm.alarmData->multiAlarms += 1;
        distributor->DistributorData(algData);
    }
}

MsgOverviewMem PosSaveSensitivity::GetOverviewInfo(const std::string& /*channelId*/,
                                                   const std::string& /*taskId*/, int64_t streamIndex,
                                                   int64_t from, int64_t to) {
    return overview_rec_inst_.GetOverviewInfo(streamIndex, from, to);
}

}  // namespace cosmo
