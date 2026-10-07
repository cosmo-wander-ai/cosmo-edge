// Qwen3VLInference.cc — Batch inference pipeline for Qwen3VLWorker.
// Split from Qwen3VLWorker.cc to reduce file size (DEBT-007).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <sstream>

#include "flow/common/AlgDataUnit.h"
#include "flow/common/FlowTaskUtil.h"
#include "flow/common/LlmYesNoJudge.h"
#include "flow/common/VisualRoi.h"
#include "flow/qwen3vl/OpenAiVlmClient.h"
#include "flow/qwen3vl/Qwen3VLWorker.h"
#include "media/VideoFrame.h"
#include "service/ai/ILlmInferService.h"
#include "service/detail/ServiceRegistry.h"
#include "service/media/IVideoFrameCodec.h"
#include "service/media/IVideoFrameOSD.h"
#include "service/media/IVideoFrameTransform.h"
#include "util/GeometricCalculation.h"
#include "util/Log.h"
#include "util/TimeUtil.h"
#include "util/UuidUtil.h"

static constexpr const char* kTag = "QWEN3VL ";
namespace cosmo {

namespace {

    bool ParseJudgeYesNoTrue(const std::string& text) {
        const auto r = ParseJudgeYesNo(text);
        if (r == LlmJudgeYesNo::Unknown) {
            LOG_WARN("{}Qwen3VL: Unrecognized judge answer (expected 是/否): {}", kTag, text);
        }
        return r == LlmJudgeYesNo::Yes;
    }

    std::string BuildJudgePrompt(const std::string& keywords, bool advanced_mode = false) {
        std::ostringstream ss;
        if (advanced_mode) {
            ss << keywords << "，回答是或者否,不要换行，不要其他内容。";
        } else {
            ss << "判断图片中是否存在【" << keywords << "】目标，回答是或者否,不要换行，不要其他内容。";
        }
        return ss.str();
    }

}  // namespace

// Must match the definition in Qwen3VLWorker.cc
struct Qwen3VLWorker::InferEntry {
    VideoFramePtr bgr_frame;
    std::string prompt;
    AlgDataPtr data;
    std::string resolved_task_id;
    VisualRoiInput crop_info;
    bool is_full_frame{false};
    Qwen3VLWorkerParamEl parameters;
    std::optional<service::VisualDecisionResult> visual_result;
};

void Qwen3VLWorker::CollectInferEntries(std::vector<AlgDataPtr>& alg_datas,
                                        std::vector<InferEntry>& entries) {
    for (auto& data : alg_datas) {
        if (!data || !data->chanDataDec.frame || !data->chanDataDec.frame->Active()) {
            continue;
        }

        std::string tid = data->taskId;

        // Resolve taskId from channel list when empty
        if (tid.empty()) {
            std::shared_lock<std::shared_mutex> lockCh(mtx);
            auto chIt = std::find_if(channel_list_.begin(), channel_list_.end(), [&data](const auto& ch) {
                return ch.channel == data->channelId && !ch.tasks.empty();
            });
            if (chIt != channel_list_.end()) {
                tid = chIt->tasks.front();
            }
        }

        auto task_params   = GetTaskParams(tid);
        std::string kw     = task_params.prompt.empty() ? std::string("目标") : task_params.prompt;
        std::string prompt = BuildJudgePrompt(kw, task_params.advanced_mode);

        std::vector<MsgTaskArea> areas;
        {
            std::shared_lock<std::shared_mutex> lock(mtx);
            const auto it = task_areas_.find(tid);
            if (it != task_areas_.end())
                areas = it->second.areas;
        }
        auto context = GetTaskContext(tid);
        const bool upstream =
            context && FlowHasUpstreamTargetSource(context->action_alg, context->action_node.flowActionId);
        for (auto& input :
             PrepareVisualRois(*data, areas, upstream, task_params.visual_judgment != nullptr)) {
            InferEntry entry;
            entry.bgr_frame        = input.frame;
            entry.prompt           = prompt;
            entry.data             = data;
            entry.resolved_task_id = tid;
            entry.is_full_frame    = input.is_full_frame;
            entry.crop_info        = std::move(input);
            entry.parameters       = task_params;
            entries.push_back(std::move(entry));
        }
    }
}

bool Qwen3VLWorker::RunBatchInference(std::vector<InferEntry>& entries, std::vector<Qwen3VLResult>& results) {
    results.clear();
    results.resize(entries.size());

    std::map<std::string, std::vector<size_t>> groups;
    for (size_t i = 0; i < entries.size(); ++i) {
        auto& entry = entries[i];
        if (entry.parameters.visual_judgment) {
            const auto source  = entry.data->chanDataDec.frame;
            const auto frameId = std::to_string(source->GetStreamIndex()) + ":" +
                                 std::to_string(source->GetFrameIndex()) + ":" +
                                 std::to_string(source->GetTimestamp());
            entry.visual_result = entry.parameters.visual_judgment->Decide(
                frameId, entry.crop_info.roi_id, entry.crop_info.area_id, [frame = entry.bgr_frame] {
                    service::VisualDecisionImage image;
                    if (!VideoFrameValid(frame))
                        return image;
                    image.width  = static_cast<int>(frame->GetWidth());
                    image.height = static_cast<int>(frame->GetHeight());
                    image.jpeg =
                        service::ServiceRegistry::Instance().Get<service::IVideoFrameCodec>().EncodeJpeg(
                            frame);
                    return image;
                });
            continue;
        }
        groups[entries[i].resolved_task_id].push_back(i);
    }

    bool ok = true;
    for (const auto& [taskId, indexes] : groups) {
        auto task_params             = entries[indexes.front()].parameters;
        task_params.generation_style = Qwen3VLGenerationStyle::RIGOROUS;
        ApplyGenerationStyle(task_params);

        std::vector<VideoFramePtr> images;
        std::vector<std::string> prompts;
        images.reserve(indexes.size());
        prompts.reserve(indexes.size());
        for (auto idx : indexes) {
            images.push_back(entries[idx].bgr_frame);
            prompts.push_back(entries[idx].prompt);
        }

        std::vector<Qwen3VLResult> group_results;
        auto ret = util::ErrorEnum::Failed;
        if (task_params.open_ai_config.Enabled()) {
            ret = OpenAiVlmClient::Generate(task_params.open_ai_config, images, prompts,
                                            task_params.gen_param, group_results);
        } else {
            ret = service::ServiceRegistry::Instance().Get<service::ILlmInferService>().Generate(
                images, prompts, task_params.gen_param, group_results);
        }

        if (ret != util::ErrorEnum::Success) {
            LOG_WARN("{}[{} {}] Generate failed taskId:{}, error:{}", kTag, alg_code_, uuid, taskId,
                     static_cast<int>(ret));
            ok = false;
            break;
        }

        for (size_t i = 0; i < indexes.size() && i < group_results.size(); ++i) {
            results[indexes[i]] = std::move(group_results[i]);
        }
    }

    if (!ok) {
        // Dispatch empty alarms on failure
        std::set<AlgDataPtr> dispatched;
        for (auto& e : entries) {
            if (dispatched.count(e.data))
                continue;
            dispatched.insert(e.data);
            e.data->taskDataAlarm.alarmData = std::make_shared<DataAlarm>();
            distributor->DistributorData(e.data->channelId, e.data,
                                         [](AlgDataPtr frame, const std::string& outTaskId) {
                                             auto outData = AlgDataCopy(frame);
                                             if (!outData)
                                                 return frame;
                                             outData->taskId = std::move(outTaskId);
                                             return outData;
                                         });
        }
        return false;
    }
    return true;
}

void Qwen3VLWorker::ProcessInferResults(std::vector<InferEntry>& entries,
                                        std::vector<Qwen3VLResult>& results) {
    // Initialize alarm structure for each AlgData
    std::map<AlgDataPtr, bool> alarm_inited;
    for (auto& e : entries) {
        if (!alarm_inited[e.data]) {
            e.data->taskDataAlarm.alarmData               = std::make_shared<DataAlarm>();
            e.data->taskDataAlarm.alarmData->flowActionId = GetTaskFlowActionId(e.resolved_task_id);
            e.data->taskDataAlarm.alarmData->multiAlarms  = 1;
            alarm_inited[e.data]                          = true;
        }
    }

    for (size_t i = 0; i < entries.size() && i < results.size(); i++) {
        auto& entry       = entries[i];
        auto& result      = results[i];
        const auto visual = entry.parameters.visual_judgment;
        // Review mode retains every candidate, including negative/unknown model
        // outcomes. Qualification and filtering are not inferred from scores.
        bool is_valid = visual ? visual->Run()->Active() : ParseJudgeYesNoTrue(result.text);

        std::string tid  = entry.resolved_task_id;
        auto task_params = entry.parameters;
        std::string kw   = task_params.prompt.empty() ? std::string("目标") : task_params.prompt;

        LOG_INFO("{}[{} {}] Qwen3VL judge taskId:{} crop_roi:[{},{},{},{}] isDetBox:{} is_valid:{} text:{}",
                 kTag, alg_code_, uuid, tid, entry.crop_info.roi.x, entry.crop_info.roi.y,
                 entry.crop_info.roi.width, entry.crop_info.roi.height, entry.crop_info.is_det_box, is_valid,
                 result.text);

        if (is_valid) {
            auto& data = entry.data;
            DataAlarmUnit unit;
            unit.flowActionId  = GetTaskFlowActionId(tid);
            unit.areaId        = entry.crop_info.area_id;
            unit.areaName      = "";
            unit.trackId       = -1;
            unit.strTrackId    = util::GenerateUUID();
            unit.reportType    = OnEventsReportType::Trigger;
            unit.bLlmPrejudged = true;
            unit.ocrString     = kw;
            if (visual && entry.visual_result) {
                unit.visualRun = visual->Run();
                unit.visualJudgments.push_back(
                    VisualRoiRecord(entry.crop_info, *entry.visual_result, unit.flowActionId));
            }

            // Set detection box for alarm overlay
            if (entry.is_full_frame) {
                auto orig_fr    = data->chanDataDec.frame;
                unit.box.x      = 0;
                unit.box.y      = 0;
                unit.box.width  = static_cast<int>(orig_fr->GetWidth());
                unit.box.height = static_cast<int>(orig_fr->GetHeight());
                unit.boxs.push_back(unit.box);
            } else {
                unit.box = entry.crop_info.roi;
                unit.boxs.push_back(entry.crop_info.roi);
            }
            CMsgOnEventsTarget target;
            target.label      = kw;
            target.confidence = visual ? 0.0F : 1.0F;
            target.trackId    = unit.strTrackId;
            target.box.x      = unit.box.x;
            target.box.y      = unit.box.y;
            target.box.width  = unit.box.width;
            target.box.height = unit.box.height;
            unit.targets.push_back(std::move(target));

            auto commit = [&] { data->taskDataAlarm.alarmData->alarms.push_back(std::move(unit)); };
            if (visual)
                visual->Run()->CommitIfCurrent(commit);
            else
                commit();
        }
    }

    // Dispatch results (once per AlgData)
    std::set<AlgDataPtr> dispatched;
    for (auto& e : entries) {
        if (dispatched.count(e.data))
            continue;
        dispatched.insert(e.data);
        auto dispatch = [&] {
            distributor->DistributorData(e.data->channelId, e.data,
                                         [&e](AlgDataPtr frame, const std::string& outTaskId) -> AlgDataPtr {
                                             if (outTaskId != e.resolved_task_id)
                                                 return nullptr;
                                             auto outData = AlgDataCopy(frame);
                                             if (!outData)
                                                 return frame;
                                             outData->taskId = std::move(outTaskId);
                                             return outData;
                                         });
        };
        if (e.parameters.visual_judgment)
            e.parameters.visual_judgment->Run()->CommitIfCurrent(dispatch);
        else
            dispatch();
    }
}

void Qwen3VLWorker::HandFrameBatch(std::vector<AlgDataPtr> alg_datas) {
    bool needs_local_model = true;
    {
        std::shared_lock<std::shared_mutex> lock(mtx);
        needs_local_model =
            params_.param.empty() ||
            std::any_of(params_.param.begin(), params_.param.end(), [](const Qwen3VLWorkerParamEl& p) {
                return !p.open_ai_config.Enabled() && p.open_ai_config.provider != "laya_v";
            });
    }

    if (needs_local_model && !local_worker_registered_) {
        service::ServiceRegistry::Instance().Get<service::ILlmInferService>().NotifyWorkerStart();
        local_worker_registered_ = true;
    } else if (!needs_local_model && local_worker_registered_) {
        service::ServiceRegistry::Instance().Get<service::ILlmInferService>().NotifyWorkerStop();
        local_worker_registered_ = false;
    }

    if (needs_local_model &&
        !service::ServiceRegistry::Instance().Get<service::ILlmInferService>().IsInitialized()) {
        action_status = util::ErrorEnum::AI_INST_NOTCREATED;
        if (!Qwen3VLSdkInit()) {
            LOG_WARN("{}[{} {}] Qwen3VL shared instance not initialized", kTag, alg_code_, uuid);
            return;
        }
    }

    // InferEntry is defined here for use by the three extracted methods
    std::vector<InferEntry> entries;
    CollectInferEntries(alg_datas, entries);

    if (entries.empty()) {
        return;
    }

    std::vector<Qwen3VLResult> results;
    if (!RunBatchInference(entries, results)) {
        return;
    }

    ProcessInferResults(entries, results);
}

}  // namespace cosmo
