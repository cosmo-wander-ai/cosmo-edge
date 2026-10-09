#include "flow/common/VisualRoi.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "service/detail/ServiceRegistry.h"
#include "service/media/IVideoFrameOSD.h"
#include "service/media/IVideoFrameTransform.h"
#include "util/GeometricCalculation.h"
#include "util/Log.h"

namespace cosmo {
namespace {
    using media::PixelFormat;
    constexpr auto kTag = "VisualRoi ";

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

    std::vector<VisualRoiInput> CropFramesForTask(const VideoFramePtr& srcFrame, const AlgData& data,
                                                  const std::vector<MsgTaskArea>& taskAreas,
                                                  bool has_upstream_target_source, bool retainInvalid = false,
                                                  bool pixelsAvailable = true) {
        std::vector<VisualRoiInput> results;
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
                VisualRoiInput cr;
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
                VisualRoiInput cr;
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

    VideoFramePtr Normalize(VideoFramePtr frame, const VideoFramePtr& source, bool typed) {
        if (!VideoFrameValid(frame))
            return nullptr;
        try {
            frame->SetFrameIndex(source->GetFrameIndex());
            frame->SetTimestamp(source->GetTimestamp());
            frame->SetStreamIndex(source->GetStreamIndex());
            const int width  = static_cast<int>(frame->GetWidth());
            const int height = static_cast<int>(frame->GetHeight());
            auto& transform  = service::ServiceRegistry::Instance().Get<service::IVideoFrameTransform>();
            if ((width > 960 || height > 960) && frame->GetPixelFormat() == PixelFormat::PIXEL_I420) {
                const double scale = std::min(960.0 / std::max(1, width), 960.0 / std::max(1, height));
                // I420 chroma planes require even dimensions on both backends.
                const int resizeHeight = std::max(32, static_cast<int>(height * scale) & ~1);
                const int resizeWidth  = std::max(32, static_cast<int>(width * scale) & ~1);
                auto resized           = transform.Resize(frame, resizeHeight, resizeWidth);
                if (VideoFrameValid(resized))
                    frame = resized;
            }
            auto bgr = ToBgrForVlm(frame);
            if (!VideoFrameValid(bgr))
                return nullptr;
            bgr->SetFrameIndex(source->GetFrameIndex());
            bgr->SetTimestamp(source->GetTimestamp());
            bgr->SetStreamIndex(source->GetStreamIndex());
            const bool transferred = transform.EnsureHostData(bgr);
            if ((typed && !transferred) || (!(transferred && bgr->GetHostData()) && !bgr->GetData()))
                return nullptr;
            return bgr;
        } catch (...) {
            if (!typed)
                throw;
            return nullptr;
        }
    }
}  // namespace

std::vector<VisualRoiInput> PrepareVisualRois(const AlgData& data, const std::vector<MsgTaskArea>& areas,
                                              bool hasUpstreamTargets, bool typed) {
    const auto source = data.chanDataDec.frame;
    if (!VideoFrameValid(source))
        return {};
    VideoFramePtr copied;
    try {
        copied = service::ServiceRegistry::Instance().Get<service::IVideoFrameOSD>().CopyJpegSrcFrame(source);
    } catch (...) {
        if (!typed)
            throw;
    }
    const bool available = VideoFrameValid(copied);
    if (!available && !typed)
        return {};
    // Failed copies use source dimensions only, never source pixels as fallback.
    const auto geometry = available ? copied : source;
    auto inputs         = CropFramesForTask(geometry, data, areas, hasUpstreamTargets, typed, available);
    if (inputs.empty() && !hasUpstreamTargets) {
        VisualRoiInput full;
        full.roi       = {0, 0, static_cast<int>(source->GetWidth()), static_cast<int>(source->GetHeight())};
        full.input_roi = full.roi;
        full.roi_id    = "full-frame";
        full.is_full_frame = true;
        full.frame         = available ? copied : nullptr;
        inputs.push_back(std::move(full));
    }
    for (auto& input : inputs)
        input.frame = Normalize(input.frame, source, typed);
    if (!typed)
        inputs.erase(std::remove_if(inputs.begin(), inputs.end(),
                                    [](const auto& input) { return !VideoFrameValid(input.frame); }),
                     inputs.end());
    return inputs;
}

nlohmann::json VisualRoiRecord(const VisualRoiInput& crop, const service::VisualDecisionResult& result,
                               const std::string& flowActionId) {
    const auto& roi     = crop.input_roi;
    const auto decision = result.response.value("decision", nlohmann::json::object());
    return {{"provider", "laya_v"},
            {"mode", decision.value("mode", "review")},
            {"business_qualified", decision.value("business_qualified", false)},
            {"alarm_filter_applied", !result.Retain()},
            {"flow_action_id", flowActionId},
            {"area_id", crop.area_id},
            {"roi_binding_policy", crop.is_det_box ? "target_center_all_matching_areas_or_default"
                                                   : "configured_area_or_full_frame"},
            {"source_target_id", crop.source_target_id},
            {"source_track_id", crop.source_track_id},
            {"source_track_index", crop.source_track_index},
            {"input_roi", {roi.x, roi.y, roi.width, roi.height}},
            {"request", result.request},
            {"audit", result.audit.metadata},
            {"result", result.response}};
}
}  // namespace cosmo
