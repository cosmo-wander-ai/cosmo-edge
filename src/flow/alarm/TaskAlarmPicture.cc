// TaskAlarmPicture.cc — Alarm picture generation.
// Implementation partition of TaskAlarm (declared in flow/alarm/TaskAlarm.h).

#include <exception>
#include <filesystem>
#include <fstream>
#include <unordered_map>

#include "flow/alarm/AlarmImageAnnotations.h"
#include "flow/alarm/AlarmImagePrivacy.h"
#include "flow/alarm/TaskAlarm.h"
#include "flow/alarm/TaskAlarmInternalTypes.h"
#include "flow/common/AreaLineUtil.h"
#include "media/Color.h"
#include "media/IOsdTextRenderer.h"
#include "media/VideoFrame.h"
#include "nlohmann/json.hpp"
#include "service/detail/ServiceRegistry.h"
#include "service/media/IVideoFrameCodec.h"
#include "service/media/IVideoFrameOSD.h"
#include "service/media/IVideoFrameTransform.h"
#include "service/model/IModelQuery.h"
#include "service/path/IFileService.h"
#include "service/system/IConfigReadService.h"
#include "util/GeometricPos.h"
#include "util/Log.h"
#include "util/NToL.h"
#include "util/PathUtil.h"

static constexpr const char* kTag = "TaskAlarm ";

namespace cosmo {
namespace {
    class PictureLabelDisplay {
    public:
        alarm::AlarmImageTargetText Target(const util::Box& box,
                                           const std::vector<AiTargetAnnotation>& annotations) {
            alarm::AlarmImageTargetText target{box, {}};
            for (const auto& annotation : annotations) {
                if (annotation.source == AiAnnotationSource::Unknown || annotation.label.empty())
                    continue;
                std::string label = annotation.label;
                try {
                    LoadResources();
                    auto& registry = service::ServiceRegistry::Instance();
                    if (!annotation.atomic_code.empty() && registry.Has<service::IModelQuery>()) {
                        const auto [model, inserted] = models_.try_emplace(annotation.atomic_code);
                        if (inserted)
                            model->second =
                                registry.Get<service::IModelQuery>().GetModelInfo(annotation.atomic_code);
                        label = alarm::ResolveAlarmTargetLabel(annotation, model->second, presetRoot_,
                                                               translations_);
                    }
                } catch (const std::exception&) {
                    // Optional display metadata must never prevent the alarm picture from being saved.
                    LOG_WARN("{}", "Alarm class display lookup failed; keeping the original class name");
                }
                target.labels.push_back(alarm::FormatAlarmTargetLabel(label, annotation.score));
            }
            return target;
        }

    private:
        void LoadResources() {
            if (loaded_)
                return;
            loaded_     = true;
            presetRoot_ = cosmo::path::GetPresetModelPath();
            std::ifstream resource(std::filesystem::path(cosmo::path::GetResourcePath()) / "i18n" /
                                   "resource.zh-CN.json");
            if (resource)
                translations_ = nlohmann::json::parse(resource, nullptr, false);
        }
        bool loaded_{false};
        std::string presetRoot_;
        nlohmann::json translations_;
        std::unordered_map<std::string, ModelInfo> models_;
    };

    void DrawPictureText(const VideoFramePtr& frame, const std::vector<alarm::AlarmImageTargetText>& targets,
                         bool showSize, const std::string& title = {}) {
        auto& registry = service::ServiceRegistry::Instance();
        auto& osd      = registry.Get<service::IVideoFrameOSD>();
        if (registry.Has<media::IOsdTextRenderer>() && registry.Get<media::IOsdTextRenderer>().IsReady()) {
            const auto text = alarm::BuildAlarmImageTextLayout(
                targets, static_cast<int>(frame->GetWidth()), static_cast<int>(frame->GetHeight()), showSize,
                registry.Get<media::IOsdTextRenderer>(), alarm::AlarmImageTextRenderScale(), title);
            if (!alarm::DrawAlarmImageText(osd, frame, text))
                LOG_WARN("{}", "Alarm image text session rejected");
            return;
        }
        // Preserve the existing size/title fallback if the optional Chinese font is absent.
        if (showSize) {
            for (const auto& target : targets) {
                const auto& box = target.box;
                osd.DrawText(frame, box.x, box.y < 30 ? box.y : box.y - 30,
                             std::to_string(box.width) + "x" + std::to_string(box.height), {255, 0, 0}, 12);
            }
        }
        if (!title.empty())
            osd.DrawText(frame, 15, 15, title, {255, 0, 0}, 15);
        if (!targets.empty())
            LOG_WARN("{}", "Alarm target labels unavailable: OSD font is not loaded");
    }
}  // namespace

void TaskAlarm::HandBestInfoPicture(CMsgOnEventsReq& msg, AlgDataPtr /*algData*/, DataAlarmUnit& alarmUnit,
                                    const util::AlarmImagePrivacy& privacy,
                                    const std::vector<std::string>& expectedDetectors, int pictureQuality) {
    media::Color box_color{0, 0, 0};
    int lineWidth = 2;
    PictureLabelDisplay labelDisplay;
    const auto overviewCfg =
        service::ServiceRegistry::Instance().Get<service::IConfigReadService>().GetPictureQuality();

    for (auto& bestInfo : alarmUnit.bestInfos) {
        if (bestInfo.bActive) {
            CMsgOnEventsPropertyPersonInfo person;
            person.box.x      = bestInfo.box.x;
            person.box.y      = bestInfo.box.y;
            person.box.width  = bestInfo.box.width;
            person.box.height = bestInfo.box.height;

            auto origImg = PrepareAlarmImage(bestInfo.bestFrame, GetTaskId(), privacy, expectedDetectors);
            if (!VideoFrameValid(origImg)) {
                msg.property.persons.push_back(person);
                continue;
            }
            auto origJpeg =
                service::ServiceRegistry::Instance().Get<service::IVideoFrameCodec>().EncodeJpeg(origImg);
            if (origJpeg.empty()) {
                LOG_WARN("{}", "ImageEmpty");
                msg.property.persons.push_back(person);
                continue;
            }

            if ((service::ServiceRegistry::Instance().Get<service::IConfigReadService>().IsNetworkModel())) {
                person.orignalPicture =
                    service::ServiceRegistry::Instance().Get<service::IFileService>().GetFileUrl(
                        service::FileType::Image);
            }

            UploadImage(msg, origJpeg, person.orignalPicture, "orig-best");

            auto boxLines = GetBoxLines(bestInfo.box, origImg->GetWidth(), origImg->GetHeight());
            service::ServiceRegistry::Instance().Get<service::IVideoFrameOSD>().DrawLines(
                origImg, boxLines, box_color, lineWidth);
            util::Box roi;
            TargetScalerParam scaleParam;
            scaleParam.scale_side = m_param.faceScaleParam;
            roi = DoScaleBox(bestInfo.box, scaleParam, origImg->GetWidth(), origImg->GetHeight());

            if (!roi.empty()) {
                auto cutImg = service::ServiceRegistry::Instance().Get<service::IVideoFrameTransform>().Crop(
                    origImg, roi);
                auto cutJpeg =
                    service::ServiceRegistry::Instance().Get<service::IVideoFrameCodec>().EncodeJpeg(cutImg);
                if (!cutJpeg.empty()) {
                    UploadImage(msg, cutJpeg, person.targetPicture, "detect-best");
                }
            }

            // Keep orig-best and the detection crop free of the new text annotations.
            if (overviewCfg.targetBoxOverview)
                DrawPictureText(origImg, {labelDisplay.Target(bestInfo.box, bestInfo.annotations)},
                                overviewCfg.targetSizeOverview);
            auto fullJpeg = service::ServiceRegistry::Instance().Get<service::IVideoFrameCodec>().EncodeJpeg(
                origImg, pictureQuality);
            if (fullJpeg.empty()) {
                LOG_WARN("{}", "ImageEmpty");
                msg.property.persons.push_back(person);
                continue;
            }

            if (service::ServiceRegistry::Instance().Get<service::IConfigReadService>().IsNetworkModel()) {
                person.fullPicture =
                    service::ServiceRegistry::Instance().Get<service::IFileService>().GetFileUrl(
                        service::FileType::Image);
            } else {
                person.fullPicture = GetJpgFileName(msg, "full-best", false);
            }

            UploadImage(msg, fullJpeg, person.fullPicture, "full-best");
            msg.property.persons.push_back(person);
        }
    }
}

void TaskAlarm::HandPicture(CMsgOnEventsReq& msg, AlgDataPtr algData, DataAlarmUnit& alarmUnit) {
    try {
        HandPictureImpl(msg, algData, alarmUnit);
    } catch (const std::exception& error) {
        LOG_WARN("[{}] Alarm image generation failed; preserve event: {}", GetTaskId(), error.what());
    } catch (...) {
        LOG_WARN("[{}] Alarm image generation failed; preserve event", GetTaskId());
    }
}

void TaskAlarm::HandPictureImpl(CMsgOnEventsReq& msg, AlgDataPtr algData, DataAlarmUnit& alarmUnit) {
    util::AlarmImagePrivacy privacy;
    std::vector<std::string> expectedDetectors;
    {
        std::shared_lock<std::shared_mutex> lock(mtx);
        privacy           = m_param.imagePrivacy;
        expectedDetectors = m_privacyExpectedDetectors;
    }
    if (privacy.Requested() && expectedDetectors.empty()) {
        LOG_WARN("[{}] Omit alarm image: detector topology is unavailable or unsupported", GetTaskId());
        return;
    }
#ifdef DURATION_LOG
    auto timpointCopyFrame = std::chrono::high_resolution_clock::now();
#endif
    // Original photo
    auto origImg = PrepareAlarmImage(algData->chanDataDec.frame, GetTaskId(), privacy, expectedDetectors);
    if (!VideoFrameValid(origImg)) {
        LOG_WARN("{}", "CopyFrame Failed");
        return;
    }

#ifdef DURATION_LOG
    auto timpointEncodeJpeg = std::chrono::high_resolution_clock::now();
#endif

    auto origJpeg = service::ServiceRegistry::Instance().Get<service::IVideoFrameCodec>().EncodeJpeg(origImg);
    if (!origJpeg.empty()) {
        if ((service::ServiceRegistry::Instance().Get<service::IConfigReadService>().IsNetworkModel())) {
            msg.orignalPicture = service::ServiceRegistry::Instance().Get<service::IFileService>().GetFileUrl(
                service::FileType::Image);
        }

        UploadImage(msg, origJpeg, msg.orignalPicture, "orig");
    }
#ifdef DURATION_LOG
    auto timpointEncodeJpegEnd = std::chrono::high_resolution_clock::now();
#endif
    if (VideoFrameValid(alarmUnit.baseFrame)) {
        auto baseImg = PrepareAlarmImage(alarmUnit.baseFrame, GetTaskId(), privacy, expectedDetectors);
        auto baseJpeg =
            VideoFrameValid(baseImg)
                ? service::ServiceRegistry::Instance().Get<service::IVideoFrameCodec>().EncodeJpeg(baseImg)
                : std::vector<uint8_t>{};
        if (!baseJpeg.empty()) {
            UploadImage(msg, baseJpeg, msg.property.machineMaterial.baseImageUrl, "base");
        }
    }

    if ((service::ServiceRegistry::Instance().Get<service::IConfigReadService>().IsNetworkModel())) {
        msg.fullPicture = service::ServiceRegistry::Instance().Get<service::IFileService>().GetFileUrl(
            service::FileType::Image);
    } else {
        msg.fullPicture    = GetJpgFileName(msg, "full", false);
        msg.orignalPicture = GetJpgFileName(msg, "orig", false);
    }

    // Detection crop photo
    util::Box roi;

    TargetScalerParam scaleParam;
    scaleParam.scale_side = m_param.faceScaleParam;
    if (alarmUnit.haveRelated) {
        if (OnEventsPropertyType::Face == m_propertyType) {
            TargetScalerParam param;
            param.scale_north = 1.4f;
            param.scale_south = 1.0f;
            param.scale_east  = 1.2f;
            param.scale_west  = 1.2f;
            roi = DoScaleBox(alarmUnit.relatedBox, param, origImg->GetWidth(), origImg->GetHeight());

            LOG_INFO("{}.{} {}x{} {}.{} {}x{}", alarmUnit.relatedBox.x, alarmUnit.relatedBox.y,
                     alarmUnit.relatedBox.width, alarmUnit.relatedBox.height, roi.x, roi.y, roi.width,
                     roi.height);
        } else {
            roi = alarmUnit.relatedBox;
        }
    } else {
        roi = DoScaleBox(alarmUnit.box, scaleParam, m_width, m_height);
    }

    LOG_INFO("DetPicture: Pos:{}.{} {}x{}  {}x{} BoxCount:{}", alarmUnit.box.x, alarmUnit.box.y,
             alarmUnit.box.width, alarmUnit.box.height, roi.width, roi.height, alarmUnit.boxs.size());

#ifdef DURATION_LOG
    auto timpointCrop    = std::chrono::high_resolution_clock::now();
    auto timpointcutJpeg = std::chrono::high_resolution_clock::now();
#endif
    if (!roi.empty()) {
        auto cutImg =
            service::ServiceRegistry::Instance().Get<service::IVideoFrameTransform>().Crop(origImg, roi);
#ifdef DURATION_LOG
        timpointcutJpeg = std::chrono::high_resolution_clock::now();
#endif
        auto cutJpeg =
            service::ServiceRegistry::Instance().Get<service::IVideoFrameCodec>().EncodeJpeg(cutImg);
        if (!cutJpeg.empty()) {
            if (service::ServiceRegistry::Instance().Get<service::IConfigReadService>().IsNetworkModel()) {
                msg.detectedPicture =
                    service::ServiceRegistry::Instance().Get<service::IFileService>().GetFileUrl(
                        service::FileType::Image);
            } else {
                msg.detectedPicture = GetJpgFileName(msg, "detect", false);
            }

            UploadImage(msg, cutJpeg, msg.detectedPicture, "detect");
        } else {
            LOG_INFO("{}", "Encode Failed");
            msg.detectedPicture = msg.fullPicture;
        }
    } else {
        msg.detectedPicture = msg.fullPicture;
    }

    if (OnEventsPropertyType::Face == m_propertyType) {
        msg.property.face.image = msg.detectedPicture;
    }
#ifdef DURATION_LOG
    auto timpointDrawBoxs = std::chrono::high_resolution_clock::now();
#endif
    // Full-scene photo
    media::Color box_color{0, 0, 0};
    int lineWidth   = 2;
    box_color.red   = 0xff;
    box_color.green = 0;
    box_color.blue  = 0;

    // VLM full-frame alarm: When Qwen3VL produces a standalone alarm (no upstream detection boxes),
    // the entire image box + target-box/size/algo-name overlay produces a meaningless full-screen red box;
    // skip decorations. However, when upstream has real detection boxes (e.g. detection → Qwen3VL), keep
    // them.
    const bool isVlmFullFrame =
        alarmUnit.bLlmPrejudged &&
        (alarmUnit.boxs.size() == 1 && alarmUnit.boxs[0].box.x == 0 && alarmUnit.boxs[0].box.y == 0 &&
         alarmUnit.boxs[0].box.width >= static_cast<int>(origImg->GetWidth()) &&
         alarmUnit.boxs[0].box.height >= static_cast<int>(origImg->GetHeight()));
    const bool skipVlmFullFrameDecor = isVlmFullFrame;

    const auto overviewCfg =
        service::ServiceRegistry::Instance().Get<service::IConfigReadService>().GetPictureQuality();

    if (!skipVlmFullFrameDecor && overviewCfg.targetBoxOverview) {
        for (const auto& overlay : alarmUnit.boxs) {  // Overlay all targets
            const auto& box = overlay.box;
            const auto lines =
                overlay.oriented_corners
                    ? GetQuadOsdLines(*overlay.oriented_corners, origImg->GetWidth(), origImg->GetHeight())
                    : GetBoxLines(box, origImg->GetWidth(), origImg->GetHeight());
            service::ServiceRegistry::Instance().Get<service::IVideoFrameOSD>().DrawLines(
                origImg, lines, box_color, lineWidth);
        }
    }

#ifdef DURATION_LOG
    auto timpointDrawArea = std::chrono::high_resolution_clock::now();
#endif

    if (overviewCfg.areaOverview) {
        // Overlay area
        auto area       = GetArea(msg.areaId);
        auto lines      = GetAreaLines(area, origImg->GetWidth(), origImg->GetHeight());
        box_color.red   = 0;
        box_color.green = 0;
        box_color.blue  = 255;

        lineWidth = 2;
        service::ServiceRegistry::Instance().Get<service::IVideoFrameOSD>().DrawLines(origImg, lines,
                                                                                      box_color, lineWidth);
    }

#ifdef DURATION_LOG
    auto timpointTrajectory = std::chrono::high_resolution_clock::now();
#endif

    if (!skipVlmFullFrameDecor && overviewCfg.targetBoxOverview) {
        // Overlay trajectory
        auto linesTrajectory = GetTrajectory(alarmUnit);
        box_color.red        = 255;
        box_color.green      = 0;
        box_color.blue       = 0;

        lineWidth = 2;
        service::ServiceRegistry::Instance().Get<service::IVideoFrameOSD>().DrawLines(
            origImg, linesTrajectory, box_color, lineWidth);
    }

    const auto title = !skipVlmFullFrameDecor && overviewCfg.alarmTypeOverview ? msg.algorithmName : "";
    std::vector<alarm::AlarmImageTargetText> textTargets;
    if (!skipVlmFullFrameDecor && overviewCfg.targetBoxOverview) {
        PictureLabelDisplay labelDisplay;
        for (const auto& overlay : alarmUnit.boxs)
            textTargets.push_back(labelDisplay.Target(overlay.box, overlay.annotations));
    }
    DrawPictureText(origImg, textTargets, overviewCfg.targetSizeOverview, title);
    // Prompt/OCR strings and VLM synthetic labels/scores remain event data, not image annotations.

#ifdef DURATION_LOG
    auto timpointEncodeJpegFull = std::chrono::high_resolution_clock::now();
#endif
    auto fullJpeg = service::ServiceRegistry::Instance().Get<service::IVideoFrameCodec>().EncodeJpeg(
        origImg, overviewCfg.picQuality);
    if (!fullJpeg.empty())
        UploadImage(msg, fullJpeg, msg.fullPicture, "full");
#ifdef DURATION_LOG
    auto timpointEnd = std::chrono::high_resolution_clock::now();
    LOG_INFO(
        "Duration: Copy:{} EncOrig:{} Calc:{} Crop:{} CutJpeg:{} DrawBox:{} DrawArea:{} Trajectory:{} "
        "EncFull:{}",
        std::chrono::duration_cast<std::chrono::milliseconds>(timpointEncodeJpeg - timpointCopyFrame).count(),
        std::chrono::duration_cast<std::chrono::milliseconds>(timpointEncodeJpegEnd - timpointEncodeJpeg)
            .count(),
        std::chrono::duration_cast<std::chrono::milliseconds>(timpointCrop - timpointEncodeJpegEnd).count(),
        std::chrono::duration_cast<std::chrono::milliseconds>(timpointcutJpeg - timpointCrop).count(),
        std::chrono::duration_cast<std::chrono::milliseconds>(timpointDrawBoxs - timpointcutJpeg).count(),
        std::chrono::duration_cast<std::chrono::milliseconds>(timpointDrawArea - timpointDrawBoxs).count(),
        std::chrono::duration_cast<std::chrono::milliseconds>(timpointTrajectory - timpointDrawArea).count(),
        std::chrono::duration_cast<std::chrono::milliseconds>(timpointEncodeJpegFull - timpointTrajectory)
            .count(),
        std::chrono::duration_cast<std::chrono::milliseconds>(timpointEnd - timpointEncodeJpegFull).count());
#endif
    HandBestInfoPicture(msg, algData, alarmUnit, privacy, expectedDetectors, overviewCfg.picQuality);
}

// Image upload — moved to TaskAlarmUpload.cc

}  // namespace cosmo
