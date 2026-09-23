#include <algorithm>

#include "flow/sensitivity/PosSaveSensitivity.h"
#include "flow/sensitivity/PosSaveSensitivityTypes.h"
#include "util/UuidUtil.h"

namespace cosmo {
void PosSaveSensitivity::HandAttributeData(AlgDataPtr data) {
    if (!attribute_schema_valid_ || !data || !data->bHaveTrack || !data->chanDataDec.frame) {
        ResetStateOnRestart();
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }
    auto input = data->dataType == AlgDataType::ChannelDataDetect ? data->chanDataDetect.detRet
                                                                  : data->GetTaskResult(data->dataType);
    if (!input || input->timestamp < 0) {
        ResetStateOnRestart();
        return;
    }
    BAPosSaveSensitivityParam params;
    uint64_t revision;
    {
        std::shared_lock<std::shared_mutex> lock(mtx);
        params   = params_;
        revision = settings_revision_;
    }
    const auto context = std::to_string(revision);
    if (observation_stream_index_ != input->streamIndex || observation_channel_ != data->channelId ||
        observation_task_ != data->taskId || observation_context_ != context) {
        ResetStateOnRestart();
        observation_stream_index_ = input->streamIndex;
        observation_channel_      = data->channelId;
        observation_task_         = data->taskId;
        observation_context_      = context;
        attribute_session_        = util::GenerateUUID();
    }
    if (input->frameIndex <= observation_frame_index_ || input->timestamp < observation_timestamp_)
        return;
    const auto now = input->timestamp;
    if (observation_timestamp_ >= 0 &&
        now - observation_timestamp_ > std::max<int64_t>(10000, params.track_lost_timeout_ms * 3)) {
        // A broken stream is not evidence of departure. Drop pending snapshots.
        map_track_id_status_.clear();
        attribute_session_ = util::GenerateUUID();
    }
    observation_frame_index_ = input->frameIndex;
    observation_timestamp_   = now;
    for (const auto& target : input->targets) {
        if (target.trackId < 0 || target.trackIdInfo.empty())
            continue;
        const bool eligible =
            !target.bFilter && target.areaSign.shielded_areas.empty() && !target.areaSign.areas.empty();
        auto found = map_track_id_status_.find(target.trackIdInfo);
        if (found == map_track_id_status_.end()) {
            // Bound retained full-frame snapshots on overloaded scenes.
            if (!eligible || map_track_id_status_.size() >= 256)
                continue;
            found                  = map_track_id_status_.try_emplace(target.trackIdInfo).first;
            auto& record           = found->second.attribute_record;
            record.recordId        = attribute_session_ + "-" + AttributeFingerprint(target.trackIdInfo);
            record.trackId         = target.trackIdInfo;
            record.schema          = attribute_schema_;
            record.schemaId        = AttributeSchemaId(attribute_schema_);
            record.firstSeen       = now;
            found->second.track_id = static_cast<unsigned>(target.trackId);
        }
        auto& state                     = found->second;
        state.last_timestamp            = static_cast<size_t>(now);
        state.attribute_record.lastSeen = now;
        if (!eligible)
            continue;
        state.attributes.Observe(attribute_schema_, target, now, params.sample_interval_ms);
        for (const auto& area : target.areaSign.areas) {
            auto& areas = state.attribute_record.areaIds;
            if (std::find(areas.begin(), areas.end(), area.area_id) == areas.end())
                areas.push_back(area.area_id);
        }
        const double quality = double(target.box.width) * target.box.height;
        if (quality > state.best_snapshot_quality) {
            state.best_snapshot_quality = static_cast<float>(quality);
            state.frame                 = data->chanDataDec.frame;
            state.target                = target;
        }
    }
    // Failed inference / missing input must not expire an unseen target.
    if (!input->observation_complete)
        return;
    for (auto it = map_track_id_status_.begin(); it != map_track_id_status_.end();) {
        auto& state = it->second;
        if (now - static_cast<int64_t>(state.last_timestamp) < params.track_lost_timeout_ms) {
            ++it;
            continue;
        }
        if (state.frame && state.attribute_record.lastSeen - state.attribute_record.firstSeen >=
                               static_cast<int64_t>(params.pos_sen_duration))
            ReportAttributeTrack(data, state, params.min_valid_count);
        it = map_track_id_status_.erase(it);
    }
    action_status = util::ErrorEnum::Success;
}

void PosSaveSensitivity::ReportAttributeTrack(AlgDataPtr data, TrackIdData& state, size_t minSamples) {
    auto record                         = state.attribute_record;
    record.attributes                   = state.attributes.Finish(attribute_schema_, minSamples);
    record.status                       = std::all_of(record.attributes.begin(), record.attributes.end(),
                                                      [](const auto& value) { return value.status == "valid"; })
                                              ? "complete"
                                              : "partial";
    auto output                         = AlgDataCopy(data);
    output->chanDataDec.reportTimeStamp = data->chanDataDec.frame->GetTimestamp();
    output->chanDataDec.frame           = state.frame;
    output->chanDataDec.native_buffer.reset();
    output->taskDataAlarm.alarmData = std::make_shared<DataAlarm>();
    DataAlarmUnit alarm;
    alarm.flowActionId = GetFlowActionId();
    alarm.trackId      = static_cast<int>(state.track_id);
    alarm.strTrackId   = record.trackId;
    alarm.box          = state.target.box;
    alarm.boxs.push_back(alarm.box);
    alarm.targets.push_back(MakeOnEventsTarget(state.target));
    if (!state.target.areaSign.areas.empty()) {
        alarm.areaId   = state.target.areaSign.areas.front().area_id;
        alarm.areaName = state.target.areaSign.areas.front().area_name;
    }
    alarm.attributeRecord = std::move(record);
    alarm.reportType      = OnEventsReportType::Realtime;
    output->taskDataAlarm.alarmData->alarms.push_back(std::move(alarm));
    output->taskDataAlarm.alarmData->multiAlarms = 1;
    distributor->DistributorData(output);
}
}  // namespace cosmo
