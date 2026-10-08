#include <set>

#include "flow/alarm/AlarmReviewRoi.h"
#include "flow/alarm/AlarmVisualState.h"
#include "flow/alarm/TaskAlarm.h"
#include "service/detail/ServiceRegistry.h"
#include "service/media/IVideoFrameCodec.h"
#include "service/model/IModelQuery.h"
#include "util/UuidUtil.h"

namespace cosmo {
bool TaskAlarm::IsTypedAlarmProvider() const {
    return m_param.llmOpenAiConfig.provider == "laya_v";
}

void TaskAlarm::InvalidateVisualAlarmPlan() {
    auto plan = std::atomic_load(&m_visualAlarmPlan);
    if (plan)
        plan->Invalidate();
}

// Caller owns m_alarmWorkMutex; no per-frame compiler calls. The action context
// is available at start/configuration time, after TaskAlarm construction.
void TaskAlarm::RebuildVisualAlarmPlan() {
    InvalidateVisualAlarmPlan();
    std::shared_ptr<AlarmVisualPlan> plan;
    if (!m_alarmStopped && IsTypedAlarmProvider() && m_param.enableLlmReview) {
        std::set<std::string> labels;
        if (m_param.llmReviewContent.empty() && !m_visualParameters.count("visual.questions")) {
            if (auto alg = GetActionAlg()) {
                std::set<std::string> models;
                for (const auto& node : alg->workFlow) {
                    const auto code = node.atomicCode.empty() ? node.atomAlgName : node.atomicCode;
                    if (code.empty() || !models.insert(code).second)
                        continue;
                    try {
                        const auto model =
                            service::ServiceRegistry::Instance().Get<service::IModelQuery>().GetModelInfo(
                                code);
                        for (const auto& label : model.labels) {
                            // ModelInfo.label is an index, not the semantic name.
                            if (!label.labelName.empty())
                                labels.insert(label.labelName);
                            if (!label.code.empty())
                                labels.insert(label.code);
                        }
                    } catch (...) {
                        // Unknown runtime labels stay explicitly unprepared.
                    }
                }
            }
        }
        auto allAreas = GetAssoAreas(m_taskArea.areas);
        std::vector<MsgTaskArea> uniqueAreas;
        std::map<std::string, nlohmann::json> seen;
        for (const auto& area : allAreas) {
            auto geometry =
                nlohmann::json{{"points", area.points}, {"box", area.pointBox}, {"params", area.params}};
            auto [found, added] = seen.emplace(area.areaId, geometry);
            if (added || found->second != geometry)
                uniqueAreas.push_back(area);  // Conflicting IDs are rejected by VisualJudgment.
        }
        plan = std::make_shared<AlarmVisualPlan>(task_id, m_param.llmReviewContent, GetAlgName(),
                                                 std::vector<std::string>(labels.begin(), labels.end()),
                                                 m_visualParameters, uniqueAreas, m_param.llmAtomicCode);
    }
    std::atomic_store(&m_visualAlarmPlan, std::move(plan));
}

void TaskAlarm::CaptureVisualAlarmCandidates(const AlgDataPtr& data) {
    auto plan = std::atomic_load(&m_visualAlarmPlan);
    if (!plan || !IsTypedAlarmProvider() || !m_param.enableLlmReview)
        return;
    const auto frameId = util::GenerateUUID();
    for (auto& unit : data->taskDataAlarm.alarmData->alarms) {
        if (unit.bLlmPrejudged || unit.reportType != OnEventsReportType::Trigger)
            continue;
        // This terminal node owns its candidates. Original targets must be
        // captured before association/batching changes scalar labels and boxes.
        unit.visualCandidates.clear();
        unit.visualRuns.push_back(plan->Run());
        AlarmVisualCandidate base;
        base.plan         = plan;
        base.frameId      = frameId;
        base.flowActionId = unit.flowActionId;
        base.areaId       = unit.areaId;
        base.trackId      = unit.strTrackId;
        base.trackIndex   = unit.trackId;
        base.box          = unit.box;
        base.confidence   = unit.confidence;
        base.attributes   = unit.attrRsts;
        if (unit.targets.size() > 1) {
            for (const auto& target : unit.targets) {
                auto candidate       = base;
                candidate.roiId      = util::GenerateUUID();
                candidate.trackId    = target.trackId;
                candidate.trackIndex = -1;  // Scalar tracking belongs to the unit, not each target.
                candidate.box        = {target.box.x, target.box.y, target.box.width, target.box.height};
                candidate.confidence = {{target.label, "", target.confidence}};
                candidate.attributes.clear();
                unit.visualCandidates.push_back(std::move(candidate));
            }
        } else {
            base.roiId = util::GenerateUUID();
            unit.visualCandidates.push_back(std::move(base));
        }
    }
}

bool TaskAlarm::ReviewVisualAlarmEvent(const CMsgOnEventsReq& event, DataAlarmUnit& unit,
                                       const VideoFramePtr& frame) {
    std::vector<bool> keep;
    for (const auto& candidate : unit.visualCandidates) {
        struct Prepared {
            std::mutex mutex;
            nlohmann::json metadata{{"preparation", "not_started"}};
        };
        auto prepared = std::make_shared<Prepared>();
        DataAlarmUnit subject;
        subject.areaId     = candidate.areaId;
        subject.confidence = candidate.confidence;
        subject.attrRsts   = candidate.attributes;
        const auto box     = candidate.box;
        // A timeout may return before preparation finishes. The queued callback
        // owns its frame/state; it must never capture a caller's stack by reference.
        auto result =
            candidate.plan->Decide(subject, candidate.frameId, candidate.roiId, [prepared, frame, box] {
                auto roi = PrepareAlarmReviewRoiStrict(frame, box);
                service::VisualDecisionImage image;
                if (VideoFrameValid(roi.frame)) {
                    try {
                        image.jpeg =
                            service::ServiceRegistry::Instance().Get<service::IVideoFrameCodec>().EncodeJpeg(
                                roi.frame);
                        if (image.jpeg.empty())
                            roi.failure = "roi_jpeg_empty";
                        else {
                            image.width  = static_cast<int>(roi.frame->GetWidth());
                            image.height = static_cast<int>(roi.frame->GetHeight());
                        }
                    } catch (...) {
                        roi.failure = "roi_jpeg_failed";
                    }
                }
                nlohmann::json metadata{
                    {"preparation", roi.failure.empty() ? "prepared" : "failed"},
                    {"reason", roi.failure},
                    {"mode", roi.mode},
                    {"padding", 48},
                    {"geometry", "axis_aligned_envelope"},
                    {"source_width", roi.sourceWidth},
                    {"source_height", roi.sourceHeight},
                    {"requested",
                     {roi.requested.x, roi.requested.y, roi.requested.width, roi.requested.height}},
                    {"actual", {roi.actual.x, roi.actual.y, roi.actual.width, roi.actual.height}}};
                {
                    std::lock_guard<std::mutex> lock(prepared->mutex);
                    prepared->metadata = std::move(metadata);
                }
                return image;
            });
        nlohmann::json geometry;
        {
            std::lock_guard<std::mutex> lock(prepared->mutex);
            geometry = prepared->metadata;
        }
        if (result.audit.lease)
            unit.visualAuditLeases.push_back(result.audit.lease);
        keep.push_back(result.Retain());
        const auto decision = result.response.value("decision", nlohmann::json::object());
        unit.visualJudgments.push_back({{"provider", "laya_v"},
                                        {"mode", decision.value("mode", "review")},
                                        {"entrypoint", "alarm_review"},
                                        {"event_id", event.messageId},
                                        {"channel_id", event.videoChannelId},
                                        {"business_qualified", decision.value("business_qualified", false)},
                                        {"alarm_filter_applied", !result.Retain()},
                                        {"target_aggregation", "any"},
                                        {"flow_action_id", candidate.flowActionId},
                                        {"area_id", candidate.areaId},
                                        {"source_track_id", candidate.trackId},
                                        {"source_track_index", candidate.trackIndex},
                                        {"alarm_box", {box.x, box.y, box.width, box.height}},
                                        {"subject_source", candidate.plan->Subject(subject).source},
                                        {"roi", std::move(geometry)},
                                        {"audit", std::move(result.audit.metadata)},
                                        {"request", std::move(result.request)},
                                        {"result", std::move(result.response)}});
    }
    return alarm::RetainVisualTargets(unit, keep);
}
}  // namespace cosmo
