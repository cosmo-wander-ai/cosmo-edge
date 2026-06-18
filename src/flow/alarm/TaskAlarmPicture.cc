// TaskAlarmPicture.cc — Alarm picture generation.
// Implementation partition of TaskAlarm (declared in flow/alarm/TaskAlarm.h).

#include <filesystem>

#include "flow/alarm/TaskAlarm.h"
#include "flow/alarm/TaskAlarmInternalTypes.h"
#include "flow/common/AreaLineUtil.h"
#include "media/Color.h"
#include "media/VideoFrame.h"
#include "service/detail/ServiceRegistry.h"
#include "service/media/IVideoFrameCodec.h"
#include "service/media/IVideoFrameOSD.h"
#include "service/media/IVideoFrameTransform.h"
#include "service/path/IFileService.h"
#include "service/system/IConfigReadService.h"
#include "util/GeometricPos.h"
#include "util/Log.h"
#include "util/NToL.h"
#include "util/PathUtil.h"

static constexpr const char* kTag = "TaskAlarm ";

namespace cosmo {

void TaskAlarm::HandBestInfoPicture(CMsgOnEventsReq& msg, AlgDataPtr /*algData*/, DataAlarmUnit& alarmUnit) {
    media::Color box_color{0, 0, 0};
    int lineWidth = 2;

    for (auto& bestInfo : alarmUnit.bestInfos) {
        if (bestInfo.bActive) {
            CMsgOnEventsPropertyPersonInfo person;
            person.box.x      = bestInfo.box.x;
            person.box.y      = bestInfo.box.y;
            person.box.width  = bestInfo.box.width;
            person.box.height = bestInfo.box.height;

            auto origImg =
                service::ServiceRegistry::Instance().Get<service::IVideoFrameOSD>().CopyJpegSrcFrame(
                    bestInfo.bestFrame);
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
            auto fullJpeg =
                service::ServiceRegistry::Instance().Get<service::IVideoFrameCodec>().EncodeJpeg(origImg);
            if (fullJpeg.empty()) {
                LOG_WARN("{}", "ImageEmpty");
                msg.property.persons.push_back(person);
                continue;
            }

            util::Box roi;
            TargetScalerParam scaleParam;
            scaleParam.scale_side = m_param.faceScaleParam;
            roi                   = DoScaleBox(bestInfo.box, scaleParam, m_width, m_height);

            if (roi.width * roi.height > 0) {
                auto cutImg = service::ServiceRegistry::Instance().Get<service::IVideoFrameTransform>().Crop(
                    bestInfo.bestFrame, roi);
                auto cutJpeg =
                    service::ServiceRegistry::Instance().Get<service::IVideoFrameCodec>().EncodeJpeg(cutImg);
                if (!cutJpeg.empty()) {
                    UploadImage(msg, cutJpeg, person.targetPicture, "detect-best");
                }
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
#ifdef DURATION_LOG
    auto timpointCopyFrame = std::chrono::high_resolution_clock::now();
#endif
    // Original photo
    auto origImg = service::ServiceRegistry::Instance().Get<service::IVideoFrameOSD>().CopyJpegSrcFrame(
        algData->chanDataDec.frame);
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
        // auto baseImg  =
        // service::ServiceRegistry::Instance().Get<service::IVideoFrameOSD>().CopyJpegSrcFrame(alarmUnit.baseFrame);
        auto baseJpeg = service::ServiceRegistry::Instance().Get<service::IVideoFrameCodec>().EncodeJpeg(
            alarmUnit.baseFrame);
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
    if (roi.width * roi.height > 0) {
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
        (alarmUnit.boxs.size() == 1 && alarmUnit.boxs[0].x == 0 && alarmUnit.boxs[0].y == 0 &&
         alarmUnit.boxs[0].width >= static_cast<int>(origImg->GetWidth()) &&
         alarmUnit.boxs[0].height >= static_cast<int>(origImg->GetHeight()));
    const bool skipVlmFullFrameDecor = isVlmFullFrame;

    const auto overviewCfg =
        service::ServiceRegistry::Instance().Get<service::IConfigReadService>().GetPictureQuality();

    if (!skipVlmFullFrameDecor && overviewCfg.targetBoxOverview) {
        for (auto box : alarmUnit.boxs) {  // Overlay all targets
            auto boxLines = GetBoxLines(box, origImg->GetWidth(), origImg->GetHeight());
            service::ServiceRegistry::Instance().Get<service::IVideoFrameOSD>().DrawLines(
                origImg, boxLines, box_color, lineWidth);

            if (overviewCfg.targetSizeOverview) {
                media::Color boxsize_color{0, 0, 0};
                boxsize_color.red       = 255;
                boxsize_color.green     = 0;
                boxsize_color.blue      = 0;
                std::string boxSizeText = std::to_string(box.width) + "x" + std::to_string(box.height);
                int posy                = 0;
                int diff                = 30;
                if (box.y < diff) {
                    posy = box.y;
                } else {
                    posy = box.y - diff;
                }
                service::ServiceRegistry::Instance().Get<service::IVideoFrameOSD>().DrawText(
                    origImg, box.x, posy, boxSizeText, boxsize_color, 12);
            }
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

    if (!skipVlmFullFrameDecor && overviewCfg.alarmTypeOverview) {
        media::Color alarmType_color{0, 0, 0};
        alarmType_color.red       = 255;
        alarmType_color.green     = 0;
        alarmType_color.blue      = 0;
        std::string alarmTypeText = msg.algorithmName;

        service::ServiceRegistry::Instance().Get<service::IVideoFrameOSD>().DrawText(
            origImg, 15, 15, alarmTypeText, alarmType_color, 15);
    }

    // VLM orchestration: overlay user keywords (stored in ocrString) onto the full-scene photo.
    // Vehicle-plate alarms still use ocrString for the plate; do not overlay in that case.
    if (!alarmUnit.ocrString.empty() && m_propertyType != OnEventsPropertyType::Vehicle) {
        media::Color promptColor{0, 0, 0};
        promptColor.red         = 0;
        promptColor.green       = 220;
        promptColor.blue        = 120;
        const bool drawAlgoName = !skipVlmFullFrameDecor && overviewCfg.alarmTypeOverview;
        int promptY             = drawAlgoName ? 42 : 15;
        service::ServiceRegistry::Instance().Get<service::IVideoFrameOSD>().DrawText(
            origImg, 15, promptY, alarmUnit.ocrString, promptColor, 16);
    }

#ifdef DURATION_LOG
    auto timpointEncodeJpegFull = std::chrono::high_resolution_clock::now();
#endif
    auto fullJpeg = service::ServiceRegistry::Instance().Get<service::IVideoFrameCodec>().EncodeJpeg(origImg);
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
    HandBestInfoPicture(msg, algData, alarmUnit);
}

// Image upload — moved to TaskAlarmUpload.cc

}  // namespace cosmo
