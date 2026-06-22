// PTaskBaseUpload.cc — Image upload and detection result handling for PTaskBase.
// Split from PTaskBase.cc to reduce file size (DEBT-007).
// Image processing algorithms (ComputeMaskPolygon, ApplyYuvMask, ApplyBgrMask) are in PTaskBaseImageProc.cc.

#include <algorithm>
#include <filesystem>

#include "flow/detect/PDinoDetector.h"
#include "flow/detect/PSamDetector.h"
#include "flow/landmark/PLandmark.h"
#include "flow/qwen3vl/PQwen3VLWorker.h"
#include "flow/recognizer/PRecognizer.h"
#include "flow/task/PTaskBase.h"
#include "media/Color.h"
#include "media/PixelFormat.h"
#include "service/detail/ServiceRegistry.h"
#include "service/media/IVideoFrameCodec.h"
#include "service/media/IVideoFrameOSD.h"
#include "service/media/IVideoFrameTransform.h"
#include "service/path/IFileService.h"
#include "util/CipherUtil.h"
#include "util/FileUtil.h"
#include "util/Keys.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/StringUtil.h"
#include "util/dto/ActionCodes.h"

namespace cosmo {

// Defined in PTaskBaseImageProc.cc
std::vector<MsgPoint> ComputeMaskPolygon(const AiMask& mask);
bool ApplyYuvMask(uint8_t* yuvData, int imgW, int imgH, const AiMask& mask);
bool ApplyBgrMask(uint8_t* bgrData, int imgW, int imgH, const AiMask& mask);

static VideoFramePtr NormalizePicInputForInference(VideoFramePtr frame) {
    if (!VideoFrameValid(frame)) {
        return nullptr;
    }
    auto pixelFormat = frame->GetPixelFormat();
    if (pixelFormat == media::PixelFormat::PIXEL_BGR8 || pixelFormat == media::PixelFormat::PIXEL_RGB8) {
        return frame;
    }
    if (pixelFormat == media::PixelFormat::PIXEL_I420) {
        return service::ServiceRegistry::Instance().Get<service::IVideoFrameTransform>().I4202BGR(frame);
    }
    LOG_WARN("Unsupported picture pixel format for inference: {}", static_cast<int>(pixelFormat));
    return nullptr;
}

static bool IsTargetInArea(const AiDetectRstEl& target, const std::string& areaId) {
    for (const auto& targetArea : target.areaSign.areas) {
        if (areaId == targetArea.area_id) {
            return true;
        }
    }
    return false;
}

static void DetTarget2MsgTarget(const AiDetectRstEl& target, MsgPTaskTarget& msgTarget) {
    msgTarget.box.x      = target.box.x;
    msgTarget.box.y      = target.box.y;
    msgTarget.box.width  = target.box.width;
    msgTarget.box.height = target.box.height;

    msgTarget.bLogicResult = target.bLogicResult;

    MsgAiConfidence confidencedet;
    confidencedet.label      = target.confidence.label;
    confidencedet.confidence = target.confidence.confidence;
    msgTarget.confidence.push_back(confidencedet);

    for (const auto& classifyRst : target.classifyRst) {
        MsgAiConfidence confidence;
        confidence.label      = classifyRst.label;
        confidence.confidence = classifyRst.confidence;
        msgTarget.confidence.push_back(confidence);
    }

    // Extract mask boundary points (Convex Hull Approximation)
    msgTarget.maskPolygon = ComputeMaskPolygon(target.mask);

    // Populate landmark keypoints
    for (const auto& pt : target.landmark.landmark) {
        MsgPoint mp;
        mp.x = pt.x;
        mp.y = pt.y;
        msgTarget.landmark.push_back(mp);
    }

    // Populate featurePreview (first 10 values)
    if (!target.feature.feature.empty()) {
        std::string preview;
        size_t count = std::min(target.feature.feature.size(), size_t(10));
        for (size_t i = 0; i < count; ++i) {
            if (i > 0)
                preview += ",";
            // 6 decimal places
            char buf[32];
            snprintf(buf, sizeof(buf), "%.6f", static_cast<double>(target.feature.feature[i]));
            preview += buf;
        }
        msgTarget.featurePreview = preview;
    }
}

static MsgPTaskArea ProcessAreaTargets(const MsgTaskArea& area, const std::vector<AiDetectRstEl>& targets,
                                       bool bHaveLogic) {
    MsgPTaskArea recArea;
    recArea.areaId     = area.areaId;
    recArea.areaName   = area.name;
    bool bAreaDetected = false;

    for (const auto& target : targets) {
        if (IsTargetInArea(target, area.areaId)) {
            MsgPTaskTarget msgTarget;
            msgTarget.bHaveLogicResult = bHaveLogic;

            DetTarget2MsgTarget(target, msgTarget);
            if (target.bLogicResult) {
                bAreaDetected = true;
            }

            recArea.targetList.push_back(msgTarget);
        }
    }

    recArea.bDetected = bAreaDetected;
    return recArea;
}

void DetData2MsgData(const std::vector<MsgTaskArea>& inAreas, DataDetTrackClassifyPtr frame,
                     std::vector<MsgPTaskArea>& outAreas, std::vector<MsgPTaskTarget>& outTargets,
                     bool bHaveLogic) {
    if (!frame) {
        return;
    }

    for (const auto& target : frame->targets) {
        MsgPTaskTarget msgTarget;
        msgTarget.bHaveLogicResult = bHaveLogic;
        DetTarget2MsgTarget(target, msgTarget);
        outTargets.push_back(msgTarget);
    }

    if (inAreas.empty()) {
        bool bAreaDetected = false;
        MsgPTaskArea area;
        area.areaId   = "-1";
        area.areaName = "default";
        for (const auto& target : frame->targets) {
            MsgPTaskTarget msgTarget;
            DetTarget2MsgTarget(target, msgTarget);
            if (target.bLogicResult) {
                bAreaDetected = true;
            }
            area.targetList.push_back(msgTarget);
        }
        area.bDetected = bAreaDetected;
        outAreas.push_back(area);
        return;
    }

    for (const auto& area : inAreas) {
        outAreas.push_back(ProcessAreaTargets(area, frame->targets, bHaveLogic));
    }
}

void RecogData2MsgData(const std::vector<MsgTaskArea>& /*inAreas*/, AlgTaskDataRecog taskDatarecog,
                       std::vector<MsgPTaskArea>& outAreas, std::vector<MsgPTaskTarget>& outTargets) {
    for (auto& area : taskDatarecog.areas) {
        if (area.bLogicResult) {
            MsgPTaskArea recArea;
            recArea.areaId   = area.areaId;
            recArea.areaName = area.areaName;
            MsgPTaskTarget msgTarget;
            msgTarget.box.x      = area.box.x;
            msgTarget.box.y      = area.box.y;
            msgTarget.box.width  = area.box.width;
            msgTarget.box.height = area.box.height;
            recArea.targetList.push_back(msgTarget);
            outAreas.push_back(recArea);

            MsgPTaskTarget outTarget;
            outTarget.bLogicResult = true;
            outTarget.box.x        = area.box.x;
            outTarget.box.y        = area.box.y;
            outTarget.box.width    = area.box.width;
            outTarget.box.height   = area.box.height;
            MsgAiConfidence score;
            score.confidence = area.average;
            score.label      = "score";
            outTarget.confidence.push_back(score);
            outTargets.push_back(outTarget);
        }
    }
}

void PTaskBase::UploadImage(std::vector<uint8_t>& data, const std::string& url, const std::string& sign) {
    std::string messageId = util::GenerateUUID();
    std::string filePath  = cosmo::path::GetRecordJsonPath();
    std::string fileName  = (std::filesystem::path(filePath) / (messageId + "_" + sign + ".jpg")).string();
    auto ret              = util::WriteFile(fileName, reinterpret_cast<const std::uint8_t*>(data.data()),
                                            static_cast<int>(data.size()));
    if (false == ret) {
        LOG_WARN("write image file Failed {}", fileName);
        return;
    }
    std::string bucket = "gaf_commodity";
    service::ServiceRegistry::Instance().Get<service::IFileService>().UploadFile(
        messageId,
        [fileName](const std::string& taskId, bool bFinished, void* /*ptr*/) {
            if (!bFinished) {
                LOG_WARN("{} Upload {} Failed", taskId, fileName);
            } else {
                LOG_INFO("{} Upload {} Success!", taskId, fileName);
            }

            // Delete file after upload
            remove(fileName.c_str());
        },
        nullptr, "jpg", fileName, bucket, url);
}

void PTaskBase::DetTargetHandFullPicture(AlgDataPtr algData, const std::vector<MsgTaskArea>& inAreas,
                                         MsgPTaskDetectPicRecv& /*data*/, MsgPTaskDetectPicSend& retData) {
    // Original image
    auto origImg = service::ServiceRegistry::Instance().Get<service::IVideoFrameOSD>().CopyJpegSrcFrame(
        algData->chanDataDec.frame);
    if (!origImg) {
        LOG_INFO("{}", "CopyFrame Failed");
        return;
    }

    // Full-scene annotated image
    media::Color box_color{uint8_t(0), uint8_t(0), uint8_t(0)};
    int lineWidth = 2;

    // Overlay detection targets with colored boxes
    box_color.red   = uint8_t(255);
    box_color.green = 0;
    box_color.blue  = 0;
    for (auto& areaTargets : retData.resData.areaList)  // Overlay all targets
    {
        for (auto& target : areaTargets.targetList) {
            // target.box.height);
            // Skip targets that have logic judgment but failed
            if ((algData->bHaveLogic) && (!target.bLogicResult)) {
                continue;
            }

            util::Box box;
            box.x         = target.box.x;
            box.y         = target.box.y;
            box.width     = target.box.width;
            box.height    = target.box.height;
            auto boxLines = GetBoxOsdLines(box, origImg->GetWidth(), origImg->GetHeight());
            service::ServiceRegistry::Instance().Get<service::IVideoFrameOSD>().DrawLines(
                origImg, boxLines, box_color, lineWidth);
        }
    }

    // Overlay landmark keypoints (green cross markers)
    if (algData->chanDataDetect.detRet) {
        media::Color landmark_color{uint8_t(0), uint8_t(255), uint8_t(0)};
        int lmLineWidth = 2;
        int imgW        = origImg->GetWidth();
        int imgH        = origImg->GetHeight();
        for (auto& target : algData->chanDataDetect.detRet->targets) {
            if (target.landmark.landmark.empty()) {
                continue;
            }
            for (auto& pt : target.landmark.landmark) {
                int px = static_cast<int>(pt.x);
                int py = static_cast<int>(pt.y);
                // Ensure within image bounds
                if (px < 2 || px >= imgW - 2 || py < 2 || py >= imgH - 2) {
                    continue;
                }
                // Draw cross marker (5x5)
                std::vector<std::pair<util::Point, util::Point>> crossLines;
                crossLines.push_back({{px - 2, py}, {px + 2, py}});
                crossLines.push_back({{px, py - 2}, {px, py + 2}});
                service::ServiceRegistry::Instance().Get<service::IVideoFrameOSD>().DrawLines(
                    origImg, crossLines, landmark_color, lmLineWidth);
            }
        }
    }

    // Overlay SAM2 semi-transparent mask
    if (algData->chanDataDetect.detRet) {
        bool hasMaskApplied = false;
        int imgW            = origImg->GetWidth();
        int imgH            = origImg->GetHeight();

        if (origImg->GetPixelFormat() == media::PixelFormat::PIXEL_I420 &&
            service::ServiceRegistry::Instance().Get<service::IVideoFrameTransform>().EnsureHostData(
                origImg)) {
            // Sophon path (I420): EnsureHostData copies device→host into GetHostData().
            // CPU path (I420): data lives in GetData() (pool memory), GetHostData() is nullptr.
            uint8_t* yuvData = origImg->GetHostData();
            if (!yuvData) {
                yuvData = origImg->GetData();  // CPU fallback
            }
            if (yuvData) {
                for (const auto& tgt : algData->chanDataDetect.detRet->targets) {
                    if (ApplyYuvMask(yuvData, imgW, imgH, tgt.mask)) {
                        hasMaskApplied = true;
                    }
                }
            }
        } else if (origImg->GetPixelFormat() == media::PixelFormat::PIXEL_BGR8) {
            // CPU path (BGR8): mask overlay directly on packed BGR data
            uint8_t* bgrData = origImg->GetData();
            if (bgrData) {
                for (const auto& tgt : algData->chanDataDetect.detRet->targets) {
                    if (ApplyBgrMask(bgrData, imgW, imgH, tgt.mask)) {
                        hasMaskApplied = true;
                    }
                }
            }
        }

        // Key: After CPU (Host) memory modification above, since EncodeJpeg
        // defaults to reading from Device memory (YUV->RGB->JPEG via hardware),
        // we must sync the modified YUV data back to Device memory.
        // CopyJpegSrcFrame detects HostData and uses bm_memcpy_s2d_partial
        // to sync it to a new device buffer, returning that new Frame.
        // On CPU backend this is a no-op copy (data is already in host memory).
        if (hasMaskApplied && origImg->GetPixelFormat() == media::PixelFormat::PIXEL_I420) {
            origImg =
                service::ServiceRegistry::Instance().Get<service::IVideoFrameOSD>().CopyJpegSrcFrame(origImg);
        }
    }

    // Overlay detection areas
    auto areaLines  = GetAreasOsdLines(inAreas, origImg->GetWidth(), origImg->GetHeight());
    box_color.red   = 0;
    box_color.green = 0;
    box_color.blue  = 0xff;
    lineWidth       = 2;
    service::ServiceRegistry::Instance().Get<service::IVideoFrameOSD>().DrawLines(origImg, areaLines,
                                                                                  box_color, lineWidth);

    auto fullJpeg = service::ServiceRegistry::Instance().Get<service::IVideoFrameCodec>().EncodeJpeg(origImg);

    // Full-scene annotated image
    retData.resData.fullPicture =
        service::ServiceRegistry::Instance().Get<service::IFileService>().GetFileUrl(
            service::FileType::Image);

    // If upload URL is unavailable (e.g. no file server in test env), fall back to Base64 encoding
    if (retData.resData.fullPicture.empty()) {
        retData.resData.fullPicture =
            "data:image/jpeg;base64," + util::EncBase64Ex(fullJpeg.data(), fullJpeg.size());
    } else {
        UploadImage(fullJpeg, retData.resData.fullPicture, "full");
    }
}

util::ErrorEnum PTaskBase::TaskDetectPic(PTaskElementPtr task, MsgPTaskDetectPicRecv& inData,
                                         MsgPTaskDetectPicSend& retData) {
    // Orchestration config is empty
    if (!task) {
        LOG_ERRO("{}", "Task Is Empty");
        return util::ErrorEnum::NotCreated;
    }

    AlgDataPtr algData                                  = std::make_shared<AlgData>();
    std::pair<util::ErrorEnum, VideoFramePtr> imageData = {util::ErrorEnum::Success, nullptr};
    if (!inData.imageBase64.empty()) {
        auto vecPicBin = std::move(util::DecBase64Vec(inData.imageBase64));
        if (vecPicBin.empty()) {
            imageData = {util::ErrorEnum::ImageContentDecryptionFailed, nullptr};
        } else {
            auto frame =
                service::ServiceRegistry::Instance().Get<service::IVideoFrameCodec>().DecodeJpeg(vecPicBin);
            if (!VideoFrameValid(frame)) {
                imageData = {util::ErrorEnum::ImageDecodeFailed, nullptr};
            } else {
                auto inferFrame = NormalizePicInputForInference(frame);
                if (!inferFrame) {
                    imageData = {util::ErrorEnum::ImageDecodeFailed, nullptr};
                } else {
                    imageData = {util::ErrorEnum::Success, inferFrame};
                }
            }
        }
    } else {
        std::vector<u_char> data;
        if (!service::ServiceRegistry::Instance().Get<service::IFileService>().DownloadFile(inData.imageUrl,
                                                                                            data)) {
            LOG_WARN("Download {} Failed", inData.imageUrl);
            imageData = {util::ErrorEnum::ImageDownloadFailed, nullptr};
        } else {
            if ((data.size() < 100) || (data.size() > media::kVideoFrameMaxSize)) {
                LOG_WARN(" Download File {} Size ({}) is out of range", inData.imageUrl, data.size());
                imageData = {util::ErrorEnum::ImageContentSizeInvalid, nullptr};
            } else {
                auto frame =
                    service::ServiceRegistry::Instance().Get<service::IVideoFrameCodec>().DecodeJpeg(data);
                if (!VideoFrameValid(frame)) {
                    imageData = {util::ErrorEnum::ImageDecodeFailed, nullptr};
                } else {
                    auto inferFrame = NormalizePicInputForInference(frame);
                    if (!inferFrame) {
                        imageData = {util::ErrorEnum::ImageDecodeFailed, nullptr};
                    } else {
                        imageData = {util::ErrorEnum::Success, inferFrame};
                    }
                }
            }
        }
    }
    if (imageData.first != util::ErrorEnum::Success) {
        LOG_INFO("{}", "Pic Dec Failed");
        return imageData.first;
    }
    algData->chanDataDec.frame = imageData.second;

    if (!algData->chanDataDec.frame || (!algData->chanDataDec.frame->Active())) {
        LOG_INFO("Pic:{} & {} Are Dec Failed", inData.imageUrl, inData.imageBase64);
        return util::ErrorEnum::ImageDecodeFailed;
    }

    std::lock_guard<std::shared_mutex> lock(task->mtx);
    for (auto& taNode : task->actions) {
        auto ret = taNode.actionInst->HandPic(algData);
        if (util::ErrorEnum::Success != ret) {
            LOG_WARN("[{} {}] {}/{} Detect Pic Failed", task->taskId, task->GetAlgName(),
                     taNode.action.actionId, taNode.action.actionName);
            return ret;
        }

        LOG_INFO("[{} {}] {}/{} Detect Pic target:{}", task->taskId, task->GetAlgName(),
                 taNode.action.actionId, taNode.action.actionName,
                 algData->chanDataDetect.detRet ? algData->chanDataDetect.detRet->targets.size() : 0);
    }
    if (AlgDataType::TaskDataRecognizer == algData->dataType) {
        RecogData2MsgData(task->params.areas, algData->taskDatarecog, retData.resData.areaList,
                          retData.resData.targetList);
        LOG_INFO("[{} {}] RecogResult:{} areaList.size:{}", task->taskId, task->GetAlgName(),
                 algData->taskDatarecog.areas.size(), retData.resData.areaList.size());
    } else {
        DetData2MsgData(task->params.areas, algData->chanDataDetect.detRet, retData.resData.areaList,
                        retData.resData.targetList, algData->bHaveLogic);
    }

    if (inData.needRetImg)
        DetTargetHandFullPicture(algData, task->params.areas, inData, retData);
    return util::ErrorEnum::Success;
}

}  // namespace cosmo
