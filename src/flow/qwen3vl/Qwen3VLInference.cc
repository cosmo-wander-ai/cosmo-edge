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
    using media::PixelFormat;

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

    struct CropResult {
        VideoFramePtr frame;
        util::Box roi;
        util::Box input_roi;
        bool is_det_box{false};
        std::string area_id;
        std::string roi_id;
        std::string source_target_id;
        std::string source_track_id;
        int source_track_index{-1};
    };

    bool IsPackedRgbFrame(const VideoFramePtr& frame) {
        if (!frame) {
            return false;
        }
        auto pf = frame->GetPixelFormat();
        return pf == PixelFormat::PIXEL_BGR8 || pf == PixelFormat::PIXEL_RGB8;
    }

    VideoFramePtr ToBgrForVlm(const VideoFramePtr& frame) {
        if (!VideoFrameValid(frame)) {
            return nullptr;
        }
        auto pf = frame->GetPixelFormat();
        if (pf == PixelFormat::PIXEL_BGR8) {
            return frame;
        }
        if (pf == PixelFormat::PIXEL_RGB8) {
            return frame;
        }
        if (pf == PixelFormat::PIXEL_I420) {
            return service::ServiceRegistry::Instance().Get<service::IVideoFrameTransform>().I4202BGR(frame);
        }
        LOG_WARN("{}Qwen3VL unsupported frame pixel format for VLM:{}", kTag, static_cast<int>(pf));
        return nullptr;
    }

    std::vector<CropResult> CropFramesForTask(const VideoFramePtr& srcFrame, const AlgData& data,
                                              const std::vector<MsgTaskArea>& taskAreas,
                                              bool has_upstream_target_source, bool retainInvalid = false,
                                              bool pixelsAvailable = true) {
        std::vector<CropResult> results;
        int img_w = static_cast<int>(srcFrame->GetWidth());
        int img_h = static_cast<int>(srcFrame->GetHeight());
        if (img_w <= 0 || img_h <= 0) {
            return results;
        }

        bool has_targets = data.chanDataDetect.detRet && !data.chanDataDetect.detRet->targets.empty();
        if (has_targets) {
            size_t index = 0;
            for (const auto& target : data.chanDataDetect.detRet->targets) {
                const auto& box = target.box;
                CropResult cr;
                cr.roi                = box;
                cr.is_det_box         = true;
                cr.roi_id             = "target-" + std::to_string(index++);
                cr.source_target_id   = target.targetId;
                cr.source_track_id    = target.trackIdInfo;
                cr.source_track_index = target.trackId;
                auto append           = [&] {
                    // Question regions select by target center. Overlapping
                    // regions yield independent results; outside all regions
                    // keeps the task default. Legacy VLM cropping is unchanged.
                    bool matched = false;
                    if (retainInvalid && box.width > 0 && box.height > 0) {
                        MsgPoint center;
                        center.x = (static_cast<double>(box.x) + box.width / 2.0) / img_w;
                        center.y = (static_cast<double>(box.y) + box.height / 2.0) / img_h;
                        for (size_t areaIndex = 0; areaIndex < taskAreas.size(); ++areaIndex) {
                            const auto& area = taskAreas[areaIndex];
                            if (area.points.size() < 3 ||
                                !std::all_of(area.points.begin(), area.points.end(),
                                                       [](const auto& point) {
                                                 return std::isfinite(point.x) && std::isfinite(point.y);
                                             }) ||
                                util::PointRelativePolygon(center, area.points.data(), area.points.size()) >
                                    0)
                                continue;
                            auto bound    = cr;
                            bound.area_id = area.areaId;
                            bound.roi_id += "-area-" + std::to_string(areaIndex);
                            results.push_back(std::move(bound));
                            matched = true;
                        }
                    }
                    if (!matched)
                        results.push_back(cr);
                };
                auto invalid = [&] {
                    if (retainInvalid)
                        append();
                };
                if (box.width <= 0 || box.height <= 0) {
                    invalid();
                    continue;
                }
                const int64_t expand_w = static_cast<int64_t>(box.width) / 5;
                const int64_t expand_h = static_cast<int64_t>(box.height) / 5;
                const int64_t roi_x    = std::max<int64_t>(0, static_cast<int64_t>(box.x) - expand_w);
                const int64_t roi_y    = std::max<int64_t>(0, static_cast<int64_t>(box.y) - expand_h);
                if (roi_x >= img_w || roi_y >= img_h) {
                    invalid();
                    continue;
                }

                const int64_t expanded_width  = static_cast<int64_t>(box.width) + 2 * expand_w;
                const int64_t expanded_height = static_cast<int64_t>(box.height) + 2 * expand_h;
                const int64_t roi_width =
                    std::min<int64_t>(static_cast<int64_t>(img_w) - roi_x, expanded_width);
                const int64_t roi_height =
                    std::min<int64_t>(static_cast<int64_t>(img_h) - roi_y, expanded_height);
                if (roi_width <= 0 || roi_height <= 0) {
                    invalid();
                    continue;
                }

                util::Box roi{static_cast<int>(roi_x), static_cast<int>(roi_y), static_cast<int>(roi_width),
                              static_cast<int>(roi_height)};
                VideoFramePtr cropped;
                try {
                    if (pixelsAvailable)
                        cropped =
                            service::ServiceRegistry::Instance().Get<service::IVideoFrameTransform>().Crop(
                                srcFrame, roi);
                } catch (...) {
                    if (!retainInvalid)
                        throw;
                }
                cr.input_roi = roi;
                if (VideoFrameValid(cropped)) {
                    cr.frame = cropped;
                    append();
                } else
                    invalid();
            }
            if (!results.empty())
                return results;
        }

        if (has_upstream_target_source) {
            return results;
        }

        if (!taskAreas.empty()) {
            size_t index = 0;
            for (const auto& area : taskAreas) {
                CropResult cr;
                cr.area_id   = area.areaId;
                cr.roi_id    = "area-" + std::to_string(index++);
                auto invalid = [&] {
                    if (retainInvalid)
                        results.push_back(cr);
                };
                double pb_w = area.pointBox.width;
                double pb_h = area.pointBox.height;
                if (!std::isfinite(pb_w) || !std::isfinite(pb_h) || !std::isfinite(area.pointBox.x) ||
                    !std::isfinite(area.pointBox.y) || pb_w <= 0.0 || pb_h <= 0.0 ||
                    std::abs(area.pointBox.x) > 2 || std::abs(area.pointBox.y) > 2 || pb_w > 3 || pb_h > 3) {
                    invalid();
                    continue;
                }
                util::Box roi;
                roi.x        = static_cast<int>(area.pointBox.x * img_w);
                roi.y        = static_cast<int>(area.pointBox.y * img_h);
                roi.width    = static_cast<int>(pb_w * img_w);
                roi.height   = static_cast<int>(pb_h * img_h);
                roi.x        = std::max(0, roi.x);
                roi.y        = std::max(0, roi.y);
                roi.width    = std::min(img_w - roi.x, roi.width);
                roi.height   = std::min(img_h - roi.y, roi.height);
                cr.roi       = roi;
                cr.input_roi = roi;
                if (roi.width <= 0 || roi.height <= 0) {
                    invalid();
                    continue;
                }
                VideoFramePtr cropped;
                try {
                    if (pixelsAvailable)
                        cropped =
                            service::ServiceRegistry::Instance().Get<service::IVideoFrameTransform>().Crop(
                                srcFrame, roi);
                } catch (...) {
                    if (!retainInvalid)
                        throw;
                }
                if (VideoFrameValid(cropped)) {
                    cr.frame = cropped;
                    results.push_back(std::move(cr));
                } else
                    invalid();
            }
        }

        return results;
    }

}  // namespace

// Must match the definition in Qwen3VLWorker.cc
struct Qwen3VLWorker::InferEntry {
    VideoFramePtr bgr_frame;
    std::string prompt;
    AlgDataPtr data;
    std::string resolved_task_id;
    CropResult crop_info;
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

        auto inFrame    = data->chanDataDec.frame;
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

        // 1) Copy source frame. CPU/x86 may keep packed BGR/RGB; Sophon usually keeps I420.
        VideoFramePtr workFrame;
        try {
            workFrame =
                service::ServiceRegistry::Instance().Get<service::IVideoFrameOSD>().CopyJpegSrcFrame(inFrame);
        } catch (...) {
            if (!task_params.visual_judgment)
                throw;
        }
        const bool pixelsAvailable = VideoFrameValid(workFrame);
        if (!pixelsAvailable && !task_params.visual_judgment) {
            LOG_WARN("{}[{} {}] CopyJpegSrcFrame failed, skip taskId:{}", kTag, alg_code_, uuid, tid);
            continue;
        }
        // Retain ROI identities for typed unknown results when copying fails.
        // inFrame supplies geometry only; its pixels are never used as fallback.
        if (!pixelsAvailable)
            workFrame = inFrame;
        workFrame->SetFrameIndex(inFrame->GetFrameIndex());
        workFrame->SetTimestamp(inFrame->GetTimestamp());
        workFrame->SetStreamIndex(inFrame->GetStreamIndex());

        // Get detection areas
        std::vector<MsgTaskArea> taskAreas;
        {
            std::shared_lock<std::shared_mutex> areaLock(mtx);
            auto it = task_areas_.find(tid);
            if (it != task_areas_.end()) {
                taskAreas = it->second.areas;
            }
        }

        // Crop: iterate all detection boxes/areas
        auto taskContext = GetTaskContext(tid);
        bool has_upstream_target_source =
            taskContext &&
            FlowHasUpstreamTargetSource(taskContext->action_alg, taskContext->action_node.flowActionId);
        auto cropResults = CropFramesForTask(workFrame, *data, taskAreas, has_upstream_target_source,
                                             task_params.visual_judgment != nullptr, pixelsAvailable);

        // Helper lambda: normalize one frame to VLM-compatible BGR/RGB and add it to inference queue.
        auto addEntry = [&](VideoFramePtr frame, const CropResult& cr, bool fullFrame) {
            auto appendEntry = [&](VideoFramePtr prepared) {
                InferEntry entry;
                entry.bgr_frame        = prepared;
                entry.prompt           = prompt;
                entry.data             = data;
                entry.resolved_task_id = tid;
                entry.crop_info        = cr;
                entry.is_full_frame    = fullFrame;
                entry.parameters       = task_params;
                entries.push_back(std::move(entry));
            };
            if (!VideoFrameValid(frame)) {
                if (task_params.visual_judgment)
                    appendEntry(nullptr);
                return;
            }
            try {
                // Inherit frame metadata
                frame->SetFrameIndex(inFrame->GetFrameIndex());
                frame->SetTimestamp(inFrame->GetTimestamp());
                frame->SetStreamIndex(inFrame->GetStreamIndex());

                // Optional scaling
                auto w                  = static_cast<int>(frame->GetWidth());
                auto h                  = static_cast<int>(frame->GetHeight());
                const int max_side      = 960;
                VideoFramePtr vlm_frame = frame;
                if ((w > max_side || h > max_side) && frame->GetPixelFormat() == PixelFormat::PIXEL_I420) {
                    double scale = std::min(static_cast<double>(max_side) / std::max(1, w),
                                            static_cast<double>(max_side) / std::max(1, h));
                    int dst_w    = std::max(32, static_cast<int>(w * scale));
                    int dst_h    = std::max(32, static_cast<int>(h * scale));
                    auto resized =
                        service::ServiceRegistry::Instance().Get<service::IVideoFrameTransform>().Resize(
                            frame, dst_h, dst_w);
                    if (VideoFrameValid(resized)) {
                        resized->SetFrameIndex(inFrame->GetFrameIndex());
                        resized->SetTimestamp(inFrame->GetTimestamp());
                        resized->SetStreamIndex(inFrame->GetStreamIndex());
                        vlm_frame = resized;
                    }
                } else if ((w > max_side || h > max_side) && IsPackedRgbFrame(frame)) {
                    LOG_INFO("{}[{} {}] Skip packed RGB resize for taskId:{}, fmt:{}, size:{}x{}", kTag,
                             alg_code_, uuid, tid, static_cast<int>(frame->GetPixelFormat()), w, h);
                }
                auto bgr = ToBgrForVlm(vlm_frame);
                if (!VideoFrameValid(bgr)) {
                    LOG_WARN("{}[{} {}] ToBgrForVlm failed, skip taskId:{}", kTag, alg_code_, uuid, tid);
                    if (task_params.visual_judgment)
                        appendEntry(nullptr);
                    return;
                }
                bgr->SetFrameIndex(inFrame->GetFrameIndex());
                bgr->SetTimestamp(inFrame->GetTimestamp());
                bgr->SetStreamIndex(inFrame->GetStreamIndex());
                auto& transform = service::ServiceRegistry::Instance().Get<service::IVideoFrameTransform>();
                bool has_host_data = transform.EnsureHostData(bgr) && bgr->GetHostData();
                if (!has_host_data && !bgr->GetData()) {
                    LOG_WARN("{}[{} {}] EnsureHostData failed, skip taskId:{}", kTag, alg_code_, uuid, tid);
                    if (task_params.visual_judgment)
                        appendEntry(nullptr);
                    return;
                }
                appendEntry(bgr);
            } catch (...) {
                if (!task_params.visual_judgment)
                    throw;
                appendEntry(nullptr);
            }
        };

        if (cropResults.empty()) {
            if (has_upstream_target_source) {
                LOG_INFO("{}[{} {}] Task:{} skip full frame (upstream target source has no targets)", kTag,
                         alg_code_, uuid, tid);
                continue;
            }
            // Full frame mode
            LOG_INFO("{}[{} {}] Task:{} using full frame (no valid box/area returned)", kTag, alg_code_, uuid,
                     tid);
            CropResult fullCr;
            fullCr.roi.x      = 0;
            fullCr.roi.y      = 0;
            fullCr.roi.width  = static_cast<int>(workFrame->GetWidth());
            fullCr.roi.height = static_cast<int>(workFrame->GetHeight());
            fullCr.is_det_box = false;
            fullCr.input_roi  = fullCr.roi;
            fullCr.roi_id     = "full-frame";
            addEntry(pixelsAvailable ? workFrame : nullptr, fullCr, true);
        } else {
            // Iterate each crop result
            for (size_t ci = 0; ci < cropResults.size(); ++ci) {
                auto& cr = cropResults[ci];
                LOG_INFO("{}[{} {}] Task:{} crop[{}/{}] roi:[{},{},{},{}] isDetBox:{}", kTag, alg_code_, uuid,
                         tid, ci, cropResults.size(), cr.roi.x, cr.roi.y, cr.roi.width, cr.roi.height,
                         cr.is_det_box);
                addEntry(cr.frame, cr, false);
            }
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
                unit.visualRun  = visual->Run();
                const auto& roi = entry.crop_info.input_roi;
                unit.visualJudgments.push_back(
                    {{"provider", "laya_v"},
                     {"mode", "review"},
                     {"business_qualified", false},
                     {"alarm_filter_applied", false},
                     {"area_id", entry.crop_info.area_id},
                     {"roi_binding_policy", entry.crop_info.is_det_box
                                                ? "target_center_all_matching_areas_or_default"
                                                : "configured_area_or_full_frame"},
                     {"source_target_id", entry.crop_info.source_target_id},
                     {"source_track_id", entry.crop_info.source_track_id},
                     {"source_track_index", entry.crop_info.source_track_index},
                     {"input_roi", {roi.x, roi.y, roi.width, roi.height}},
                     {"request", entry.visual_result->request},
                     {"result", entry.visual_result->response}});
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
