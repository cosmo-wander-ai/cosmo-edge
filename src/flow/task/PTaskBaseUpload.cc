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

void DetTarget2MsgTarget(const AiDetectRstEl& target, MsgPTaskTarget& msgTarget) {
    msgTarget.box.x      = target.box.x;
    msgTarget.box.y      = target.box.y;
    msgTarget.box.width  = target.box.width;
    msgTarget.box.height = target.box.height;

    msgTarget.bLogicResult = target.bLogicResult;
    for (const auto& attr : target.attrRst)
        msgTarget.attributes.push_back({attr.category, attr.label});
    for (const auto& text : target.ocrRst)
        msgTarget.texts.push_back(text.value);
    if (target.matchInfo.setPicCount >= 0) {
        msgTarget.bHaveMatchInfo         = true;
        msgTarget.matchInfo.setPicCount  = target.matchInfo.setPicCount;
        msgTarget.matchInfo.matchId      = target.matchInfo.match_id;
        msgTarget.matchInfo.matchDegree  = target.matchInfo.match_degree;
        msgTarget.matchInfo.groupId      = target.matchInfo.group_id;
        msgTarget.matchInfo.groupName    = target.matchInfo.group_name;
        msgTarget.matchInfo.matched      = target.matchInfo.matched;
        msgTarget.matchInfo.name         = target.matchInfo.name;
        msgTarget.matchInfo.baseImageUrl = target.matchInfo.base_image_url;
        msgTarget.matchInfo.personId     = target.matchInfo.person_id;
        msgTarget.matchInfo.personCode   = target.matchInfo.person_code;
    }

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
                remove(fileName.c_str());
            }
        },
        nullptr, "jpg", fileName, bucket, url);
}

void PTaskBase::DetTargetHandFullPicture(AlgDataPtr algData, MsgPTaskDetectPicRecv& /*data*/,
                                         MsgPTaskDetectPicSend& retData) {
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
    if (algData->chanDataDetect.detRet) {
        for (const auto& target : algData->chanDataDetect.detRet->targets) {
            if (target.box.width <= 0 || target.box.height <= 0)
                continue;
            auto boxLines = GetBoxOsdLines(target.box, origImg->GetWidth(), origImg->GetHeight());
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

namespace {
    std::pair<util::ErrorEnum, VideoFramePtr> DecodePictureInput(MsgPictureInput& input) {
        const int sources = int(!input.imageData.empty() || !input.uploadId.empty()) +
                            int(!input.imageBase64.empty()) + int(!input.imageUrl.empty());
        if (sources != 1 || (!input.uploadId.empty() && input.imageData.empty()))
            return {util::ErrorEnum::InvalidParam, nullptr};
        std::vector<uint8_t> bytes = std::move(input.imageData);
        if (!input.imageBase64.empty())
            bytes = util::DecBase64Vec(input.imageBase64);
        if (!input.imageUrl.empty() &&
            !service::ServiceRegistry::Instance().Get<service::IFileService>().DownloadFile(input.imageUrl,
                                                                                            bytes))
            return {util::ErrorEnum::ImageDownloadFailed, nullptr};
        if (bytes.empty() || bytes.size() > media::kVideoFrameMaxSize)
            return {util::ErrorEnum::ImageContentSizeInvalid, nullptr};
        auto frame = service::ServiceRegistry::Instance().Get<service::IVideoFrameCodec>().DecodeJpeg(bytes);
        frame      = NormalizePicInputForInference(frame);
        if (!VideoFrameValid(frame))
            return {util::ErrorEnum::ImageDecodeFailed, nullptr};
        return {util::ErrorEnum::Success, frame};
    }
}  // namespace

util::ErrorEnum PTaskBase::TaskDetectPic(PTaskElementPtr task, MsgPTaskDetectPicRecv& inData,
                                         MsgPTaskDetectPicSend& retData) {
    if (!task)
        return util::ErrorEnum::NotCreated;
    const bool paired      = std::any_of(task->actions.begin(), task->actions.end(),
                                         [](const auto& e) { return e.action.actionId == PAPairMatch_Code; });
    retData.resData.status = "failed";
    if (paired && !inData.referenceImage.HasInput()) {
        retData.resData.errorSide = "B";
        return util::ErrorEnum::PicturePairInputRequired;
    }
    if (!paired && inData.referenceImage.HasInput())
        return util::ErrorEnum::PicturePairUnexpectedReference;
    MsgPictureInput primary{inData.imageBase64, inData.imageUrl, inData.uploadId,
                            std::move(inData.imageData)};
    if (paired && !primary.HasInput()) {
        retData.resData.errorSide = "A";
        return util::ErrorEnum::PicturePairInputRequired;
    }
    auto [status, frame] = DecodePictureInput(primary);
    if (status != util::ErrorEnum::Success) {
        if (paired)
            retData.resData.errorSide = "A";
        return status;
    }
    auto input               = std::make_shared<AlgData>();
    input->chanDataDec.frame = frame;
    AlgDataPtr peer;
    if (paired) {
        auto [peerStatus, peerFrame] = DecodePictureInput(inData.referenceImage);
        if (peerStatus != util::ErrorEnum::Success) {
            retData.resData.errorSide = "B";
            return peerStatus;
        }
        peer                    = std::make_shared<AlgData>();
        peer->chanDataDec.frame = peerFrame;
    }
    AlgDataPtr rendered, referenceRendered;
    status = ExecutePicture(task, input, inData, retData, rendered, peer, &referenceRendered);
    if (status != util::ErrorEnum::Success)
        return status;
    if (inData.needRetImg && rendered)
        DetTargetHandFullPicture(rendered, inData, retData);
    if (inData.needRetImg && referenceRendered) {
        MsgPTaskDetectPicSend referenceResponse;
        DetTargetHandFullPicture(referenceRendered, inData, referenceResponse);
        retData.resData.referencePicture = referenceResponse.resData.fullPicture;
    }
    return util::ErrorEnum::Success;
}

}  // namespace cosmo
