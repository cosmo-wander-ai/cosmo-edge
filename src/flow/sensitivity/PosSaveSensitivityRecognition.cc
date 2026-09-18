#include <algorithm>
#include <cmath>

#include "flow/sensitivity/PosSaveSensitivity.h"
#include "flow/sensitivity/PosSaveSensitivityTypes.h"

namespace cosmo {

void PosSaveSensitivity::ResetStateOnRestart() {
    map_track_id_status_.clear();
    recognition_context_.clear();
    recognition_channel_.clear();
    recognition_task_.clear();
    recognition_stream_index_ = -1;
    recognition_frame_index_  = -1;
    recognition_timestamp_    = -1;
    dec_frame_.reset();
    should_alarm_ = false;
}

void PosSaveSensitivity::HandRecognitionData(AlgDataPtr data) {
    if (!data || data->dataType != AlgDataType::TaskDataRecognizer || !data->bHaveTrack ||
        !data->chanDataDec.frame) {
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }
    auto input = data->GetTaskResult(AlgDataType::TaskDataRecognizer);
    if (!input || input->timestamp < 0 || input->recognition_context.empty()) {
        return;
    }
    BAPosSaveSensitivityParam params;
    {
        std::shared_lock<std::shared_mutex> lock(mtx);
        params = params_;
    }
    if (recognition_stream_index_ != input->streamIndex || recognition_channel_ != data->channelId ||
        recognition_task_ != data->taskId || recognition_context_ != input->recognition_context) {
        ResetStateOnRestart();
        recognition_stream_index_ = input->streamIndex;
        recognition_channel_      = data->channelId;
        recognition_task_         = data->taskId;
        recognition_context_      = input->recognition_context;
    }
    // Duplicate or delayed packets are not new observations and cannot expire tracks.
    if (input->frameIndex <= recognition_frame_index_ || input->timestamp < recognition_timestamp_) {
        return;
    }
    const int64_t now = input->timestamp;
    const bool input_gap =
        recognition_timestamp_ >= 0 &&
        now - recognition_timestamp_ > std::max<int64_t>(10000, params.track_lost_timeout_ms * 3);
    if (input_gap || !input->observation_complete) {
        // Retain known identities; never turn an interrupted observation into a stranger.
        for (auto& [key, state] : map_track_id_status_) {
            state.face_evidence.Invalidate();
        }
    }
    recognition_frame_index_ = input->frameIndex;
    recognition_timestamp_   = now;
    for (const auto& target : input->targets) {
        if (target.trackId < 0 || target.trackIdInfo.empty()) {
            continue;
        }
        auto& state = map_track_id_status_[target.trackIdInfo];
        if (state.track_id_uuid.empty()) {
            state.track_id        = static_cast<unsigned>(target.trackId);
            state.track_id_uuid   = target.trackIdInfo;
            state.first_timestamp = static_cast<size_t>(now);
        }
        state.last_timestamp      = static_cast<size_t>(now);
        const auto observation    = target.face_observation.status;
        TrackObservation evidence = TrackObservation::kSkipped;
        if (!input->observation_complete || observation == FaceObservationStatus::kUnavailable) {
            evidence = TrackObservation::kUnavailable;
        } else if (observation == FaceObservationStatus::kMatched) {
            evidence = TrackObservation::kPositive;
        } else if (observation == FaceObservationStatus::kUnmatched && !target.bFilter &&
                   target.relatedEl.bActive && std::isfinite(target.face_observation.quality) &&
                   target.areaSign.shielded_areas.empty() && !target.areaSign.areas.empty()) {
            evidence = TrackObservation::kNegative;
        }
        state.face_evidence.Observe(now, evidence, params.face_sample_interval_ms);
        if (state.face_evidence.IsPositive()) {
            state.frame.reset();
            continue;
        }
        if (evidence != TrackObservation::kNegative) {
            continue;
        }
        if (target.face_observation.quality > state.best_face_quality) {
            state.best_face_quality = target.face_observation.quality;
            state.frame             = data->chanDataDec.frame;
            state.target            = target;
        }
    }
    // Only a healthy frame can prove absence. No wall-clock timer expires tracks during a stalled stream.
    if (!input->observation_complete) {
        return;
    }
    for (auto it = map_track_id_status_.begin(); it != map_track_id_status_.end();) {
        auto& state = it->second;
        if (!state.face_evidence.HasExpired(now, params.track_lost_timeout_ms)) {
            ++it;
            continue;
        }
        if (state.frame &&
            state.face_evidence.CanAlarm(params.min_valid_face_count, params.pos_sen_duration)) {
            ReportUnmatchedTrack(data, state);
        }
        it = map_track_id_status_.erase(it);
    }
    action_status = util::ErrorEnum::Success;
}

void PosSaveSensitivity::ReportUnmatchedTrack(AlgDataPtr data, TrackIdData& state) {
    // Each track/area owns an envelope and its matching evidence frame. Simultaneous
    // departures must not overwrite another track's picture or discard its alarm.
    for (const auto& area : state.target.areaSign.areas) {
        auto output                         = AlgDataCopy(data);
        output->chanDataDec.reportTimeStamp = data->chanDataDec.frame->GetTimestamp();
        output->chanDataDec.frame           = state.frame;
        output->chanDataDec.native_buffer.reset();
        output->taskDataAlarm.alarmData = std::make_shared<DataAlarm>();
        auto& alarm_data                = *output->taskDataAlarm.alarmData;
        DataAlarmUnit alarm;
        alarm.flowActionId = action_node.flowActionId;
        alarm.trackId      = static_cast<int>(state.track_id);
        alarm.strTrackId   = state.track_id_uuid;
        alarm.areaId       = area.area_id;
        alarm.areaName     = area.area_name;
        alarm.box          = state.target.box;
        alarm.boxs.push_back(alarm.box);
        alarm.haveRelated = true;
        alarm.relatedBox  = state.target.relatedEl.box;
        alarm.confidence  = state.target.relatedEl.classifyRst;
        alarm.feature     = state.target.feature;
        alarm.matchInfo   = state.target.matchInfo;
        alarm.targets.push_back(MakeOnEventsTarget(state.target));
        alarm.reportType = OnEventsReportType::Trigger;
        alarm_data.alarms.push_back(std::move(alarm));
        alarm_data.multiAlarms = 1;
        distributor->DistributorData(output);
    }
}

}  // namespace cosmo
