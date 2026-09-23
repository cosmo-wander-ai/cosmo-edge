#include <algorithm>
#include <cmath>

#include "flow/sensitivity/PosSaveSensitivity.h"
#include "flow/sensitivity/PosSaveSensitivityTypes.h"
#include "flow/sensitivity/ResultAccumulationInput.h"

namespace cosmo {

void PosSaveSensitivity::ResetStateOnRestart() {
    attribute_session_.clear();
    map_track_id_status_.clear();
    observation_context_.clear();
    observation_channel_.clear();
    observation_task_.clear();
    observation_stream_index_ = -1;
    observation_frame_index_  = -1;
    observation_timestamp_    = -1;
    dec_frame_.reset();
    should_alarm_ = false;
}

void PosSaveSensitivity::HandAccumulationData(AlgDataPtr data) {
    const auto invalidate = [this] {
        for (auto& [key, state] : map_track_id_status_) {
            state.evidence.Invalidate();
        }
    };
    if (!data || !data->bHaveTrack || !data->chanDataDec.frame) {
        invalidate();
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }
    const bool recognition = data->dataType == AlgDataType::TaskDataRecognizer;
    if (!recognition && input_mode_ == InputMode::Recognition) {
        invalidate();
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }
    auto input = data->dataType == AlgDataType::ChannelDataDetect ? data->chanDataDetect.detRet
                 : data->dataType == AlgDataType::TaskDataClassifyMultPic
                     ? data->taskDataClassifyMultPic.classifyRst
                     : data->GetTaskResult(data->dataType);
    if (!input || input->timestamp < 0 ||
        (recognition ? input->recognition_context.empty() : input->logic_context.empty())) {
        // Raw detector/default false values do not constitute completed judgments.
        invalidate();
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }
    BAPosSaveSensitivityParam params;
    uint64_t revision;
    {
        std::shared_lock<std::shared_mutex> lock(mtx);
        params   = params_;
        revision = settings_revision_;
    }
    const auto context =
        (recognition ? "recognition:" + input->recognition_context : "logic:" + input->logic_context) +
        ":settings:" + std::to_string(revision);
    if (observation_stream_index_ != input->streamIndex || observation_channel_ != data->channelId ||
        observation_task_ != data->taskId || observation_context_ != context) {
        ResetStateOnRestart();
        observation_stream_index_ = input->streamIndex;
        observation_channel_      = data->channelId;
        observation_task_         = data->taskId;
        observation_context_      = context;
    }
    // Duplicate or delayed packets are not new observations and cannot expire tracks.
    if (input->frameIndex <= observation_frame_index_ || input->timestamp < observation_timestamp_) {
        return;
    }
    const int64_t now = input->timestamp;
    const bool input_gap =
        observation_timestamp_ >= 0 &&
        now - observation_timestamp_ > std::max<int64_t>(10000, params.track_lost_timeout_ms * 3);
    if (input_gap || !input->observation_complete) {
        // Retain known identities; never turn an interrupted observation into a stranger.
        invalidate();
    }
    observation_frame_index_ = input->frameIndex;
    observation_timestamp_   = now;
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
        state.last_timestamp = static_cast<size_t>(now);
        const auto observation =
            AssessAccumulationObservation(target, input->observation_complete, recognition);
        state.evidence.Observe(now, observation.status, params.sample_interval_ms);
        if (state.evidence.IsPositive()) {
            state.frame.reset();
            continue;
        }
        if (observation.status != TrackObservation::kNegative) {
            continue;
        }
        if (observation.snapshot_quality > state.best_snapshot_quality) {
            state.best_snapshot_quality = observation.snapshot_quality;
            state.frame                 = data->chanDataDec.frame;
            state.target                = target;
        }
    }
    // Only a healthy frame can prove absence. No wall-clock timer expires tracks during a stalled stream.
    if (!input->observation_complete) {
        return;
    }
    for (auto it = map_track_id_status_.begin(); it != map_track_id_status_.end();) {
        auto& state = it->second;
        if (!state.evidence.HasExpired(now, params.track_lost_timeout_ms)) {
            ++it;
            continue;
        }
        if (state.frame && state.evidence.CanAlarm(params.min_valid_count, params.pos_sen_duration)) {
            ReportUnmatchedTrack(data, state, recognition);
        }
        it = map_track_id_status_.erase(it);
    }
    action_status = util::ErrorEnum::Success;
}

void PosSaveSensitivity::ReportUnmatchedTrack(AlgDataPtr data, TrackIdData& state, bool recognition) {
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
        alarm.haveRelated = state.target.relatedEl.bActive;
        if (alarm.haveRelated) {
            alarm.relatedBox = state.target.relatedEl.box;
        }
        alarm.confidence = alarm.haveRelated ? state.target.relatedEl.classifyRst : state.target.classifyRst;
        alarm.feature    = state.target.feature;
        // A below-threshold candidate is not an identified person. Stranger events
        // must not carry its identity, library photo or matching score.
        alarm.matchInfo = recognition ? AiDetectMatchHighScoreInfo{} : state.target.matchInfo;
        alarm.targets.push_back(MakeOnEventsTarget(state.target));
        alarm.reportType = OnEventsReportType::Trigger;
        alarm_data.alarms.push_back(std::move(alarm));
        alarm_data.multiAlarms = 1;
        distributor->DistributorData(output);
    }
}

}  // namespace cosmo
