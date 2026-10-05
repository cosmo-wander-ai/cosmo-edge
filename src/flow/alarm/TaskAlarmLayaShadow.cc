#include "flow/alarm/AlarmReviewRoi.h"
#include "flow/alarm/LayaReview.h"
#include "flow/alarm/TaskAlarm.h"
#include "service/detail/ServiceRegistry.h"
#include "service/media/IVideoFrameCodec.h"
#include "util/Log.h"
#include "util/TimeUtil.h"

namespace cosmo {
bool TaskAlarm::ReviewLayaEvent(const CMsgOnEventsReq& event, const DataAlarmUnit& alarmUnit,
                                const VideoFramePtr& frame, bool review, bool stored) {
    try {
        auto run                = std::atomic_load(&m_layaShadowRun);
        nlohmann::json identity = {
            {"event_id", event.messageId},
            {"task_id", task_id},
            {"run_epoch", run->epoch},
            {"channel_id", channel},
            {"channel_name", event.channelName},
            {"created_ms", util::GetMilliseconds()},
            {"frame_timestamp", frame ? frame->GetTimestamp() : 0},
            {"mode", review ? "review" : "observe"},
            {"question_id", "helmet-review-en-v1"},
            {"question_version", 1},
            {"policy_version", "helmet-conservative-v1"},
            {"publication", review ? "pending" : (stored ? "published" : "alarm_store_failed")},
            {"target_count", alarmUnit.targets.size()}};
        // One independently retained frame per admitted job. All crop/encode work is off the alarm thread.
        return LayaReview::Instance().Submit(
            std::move(identity), run,
            [frame, alarmUnit] {
                LayaShadowPayload payload;
                // A union containing several people cannot prove that every target wears a helmet.
                if (!VideoFrameValid(frame) || alarmUnit.targets.size() > 1 || alarmUnit.box.empty())
                    return payload;
                auto roi = PrepareAlarmReviewRoi(frame, alarmUnit.box);
                if (!VideoFrameValid(roi.frame))
                    return payload;
                payload.metadata = {{"image_width", roi.frame->GetWidth()},
                                    {"image_height", roi.frame->GetHeight()},
                                    {"roi_mode", roi.mode},
                                    {"geometry", "axis_aligned_envelope"},
                                    {"padding", 48}};
                payload.jpeg =
                    service::ServiceRegistry::Instance().Get<service::IVideoFrameCodec>().EncodeJpeg(
                        roi.frame);
                return payload;
            },
            review);
    } catch (...) {
        LOG_WARN("Laya review submission failed; alarm retained");
        return true;
    }
}
void TaskAlarm::ObserveLayaShadow(const DataAlarmUnit& alarmUnit, const VideoFramePtr& frame) {
    try {
        auto& observer = LayaShadowObserver::Instance();
        if (!observer.EnabledForTask(task_id))
            return;
        auto run                = std::atomic_load(&m_layaShadowRun);
        nlohmann::json identity = {{"channel_id", channel},
                                   {"flow_action_id", GetFlowActionId()},
                                   {"track_id", alarmUnit.trackId},
                                   {"track_id_text", alarmUnit.strTrackId},
                                   {"frame_index", frame ? frame->GetFrameIndex() : 0},
                                   {"frame_timestamp", frame ? frame->GetTimestamp() : 0},
                                   {"stream_index", frame ? frame->GetStreamIndex() : -1}};
        observer.Submit(task_id, run, std::move(identity), [&] {
            LayaShadowPayload payload;
            payload.metadata = {{"roi_mode", "invalid"}};
            if (!VideoFrameValid(frame) || alarmUnit.box.empty())
                return payload;
            auto roi         = PrepareAlarmReviewRoi(frame, alarmUnit.box);
            payload.metadata = {
                {"roi_mode", roi.mode},
                {"geometry", "axis_aligned_envelope"},
                {"padding", 48},
                {"source_width", roi.sourceWidth},
                {"source_height", roi.sourceHeight},
                {"alarm_box", {alarmUnit.box.x, alarmUnit.box.y, alarmUnit.box.width, alarmUnit.box.height}},
                {"requested_roi",
                 {roi.requested.x, roi.requested.y, roi.requested.width, roi.requested.height}},
                {"actual_roi", {roi.actual.x, roi.actual.y, roi.actual.width, roi.actual.height}}};
            // Original oriented corners remain diagnostic metadata. No rotated crop is implied.
            payload.metadata["oriented_targets"] = nlohmann::json::array();
            for (const auto& target : alarmUnit.targets) {
                if (!target.oriented_corners)
                    continue;
                nlohmann::json corners = nlohmann::json::array();
                for (const auto& point : *target.oriented_corners)
                    corners.push_back({point.x, point.y});
                payload.metadata["oriented_targets"].push_back(
                    {{"track_id", target.trackId}, {"corners", corners}});
            }
            if (!VideoFrameValid(roi.frame))
                return payload;
            payload.metadata["image_width"]  = roi.frame->GetWidth();
            payload.metadata["image_height"] = roi.frame->GetHeight();
            payload.jpeg =
                service::ServiceRegistry::Instance().Get<service::IVideoFrameCodec>().EncodeJpeg(roi.frame);
            return payload;
        });
    } catch (...) {
        // An optional diagnostic observer must not interrupt the pre-existing alarm path.
        LOG_WARN("TaskAlarm [{}] Laya shadow observation failed; alarm decision unchanged", task_id);
    }
}
}  // namespace cosmo
