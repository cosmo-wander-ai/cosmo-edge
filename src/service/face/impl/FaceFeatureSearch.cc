// FaceFeatureExtractorSearch.cc — Database search and feature comparison for FaceFeatureExtractor.
// Split from FaceFeatureExtractor.cc to reduce file size (DEBT-007).

#include <chrono>
#include <thread>

#include "media/PixelFormat.h"
#include "service/detail/ServiceRegistry.h"
#include "service/face/impl/FaceFeatureExtractor.h"
#include "service/media/IVideoFrameCodec.h"
#include "service/media/IVideoFrameTransform.h"
#include "service/media/dto/DetectMsgTypes.h"
#include "service/model/IModelPathMapping.h"
#include "service/model/IModelService.h"
#include "util/AiTypes.h"
#include "util/CipherUtil.h"
#include "util/DurationLogger.h"
#include "util/FileUtil.h"
#include "util/GeometricPos.h"
#include "util/InferConstants.h"
#include "util/Keys.h"
#include "util/Log.h"
#include "util/NToL.h"
#include "util/TimeUtil.h"
#include "util/TimingConstants.h"
#include "util/UuidUtil.h"

static constexpr const char* kTag = "SRV-FACEFEATURE ";
namespace cosmo {

bool FaceFeatureExtractor::FaceFeature(VideoFramePtr frame, std::vector<AiDetectRstEl>& ioPuts) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    if (!is_ready_) {
        return false;
    }
    if (!recog_inst_) {
        return false;
    }
    auto ret = recog_inst_->Extract(frame, ioPuts);
    if (util::ErrorEnum::Success != ret) {
        return false;
    }
    return true;
}

bool FaceFeatureExtractor::HandMsgFaceDetect(VideoFramePtr frame, MsgGetFeaturesFeature& feature,
                                             std::vector<AiDetectRstEl>& rst) {
    bool is_inited = false;

    auto faces = FaceDetect(frame, is_inited);
    LOG_INFO("{}FaceDetect result, inited:{}, face_count:{}, image:{}x{}", kTag, is_inited, faces.size(),
             frame ? frame->GetWidth() : 0, frame ? frame->GetHeight() : 0);
    if (faces.empty()) {
        if (!is_inited) {
            LOG_INFO("{}", "Ai Inst Not Create!");
            feature.code = feature_code::kNoAiInst;
        } else {
            LOG_INFO("{}", "No Faces!");
            feature.code = feature_code::kNoFace;
        }
        return false;
    } else if (faces.size() > 1) {
        for (auto& face : faces) {
            MsgRectReal rect;
            rect.x      = face.box.x;
            rect.y      = face.box.y;
            rect.width  = face.box.width;
            rect.height = face.box.height;
            feature.rects.push_back(rect);
        }
        feature.code = feature_code::kMultFaces;
        LOG_INFO("Too Much Faces! {}", faces.size());
        return false;
    } else {
        MsgRectReal rect;
        rect.x      = faces[0].box.x;
        rect.y      = faces[0].box.y;
        rect.width  = faces[0].box.width;
        rect.height = faces[0].box.height;
        feature.rects.push_back(rect);
        LOG_INFO("{}FaceDetect bbox x:{} y:{} w:{} h:{}", kTag, rect.x, rect.y, rect.width, rect.height);
        faces[0].bForceClassify = true;
    }
    rst = faces;
    return true;
}

/*
frontFace:0.0027254522
slantedFace:0.9930965
fullFace:0.00018012524
faceBlur0:2.1755695e-06
faceBlur1:8.9883804e-05
faceBlur2:0.99991703
*/
bool FaceFeatureExtractor::GetQualityAngle(const std::vector<AiConfidence>& classifyRst,
                                           MsgGetFeaturesFeature& feature, float quality) {
    if (6 != classifyRst.size()) {
        LOG_WARN("Face Classify.size():{} Is Not 6", classifyRst.size());
        feature.code = feature_code::kAiFailed;
        return false;
    }

    AiConfidence face_quality;
    AiConfidence face_angle;
    if (!GetFaceQuality(classifyRst, face_quality)) {
        LOG_WARN("{}", "get face quality failed");
        feature.code = feature_code::kAiFailed;
        return false;
    }
    if (!GetFaceAngle(classifyRst, face_angle)) {
        LOG_WARN("{}", "get face angle failed");
        feature.code = feature_code::kAiFailed;
        return false;
    }
    FaceAngle angle = FaceAngle::FaceAngleFull;
    if (!GetFaceAngleType(face_angle, angle)) {
        LOG_WARN("get face angle type failed, label:{}", face_angle.label);
        feature.code = feature_code::kAiFailed;
        return false;
    }

    if (angle != FaceAngle::FaceAngleFront) {
        LOG_WARN("get face angle is {}", angle);
        feature.code = feature_code::kAngle;
        return false;
    }

    if (face_quality.confidence < quality) {
        LOG_WARN("get face quality is {} {}", face_quality.confidence, quality);
        feature.code = feature_code::kQuality;
        return false;
    }

    return true;
}

float FaceFeatureExtractor::CalculateScore(const AiFeature& feature1, const AiFeature& feature2) {
    if (feature1.feature.empty() || feature2.feature.empty()) {
        return 0.f;
    }
    size_t feature_size = feature1.feature.size();
    if (feature1.feature.size() != feature2.feature.size()) {
        LOG_ERRO("error feature, f1: {}, f2: {}", feature1.feature.size(), feature2.feature.size());
        return kFeatureDimensionMismatch;
    }

    float sum = 0.0f;
    for (size_t i = 0; i < feature_size; ++i) {
        sum += feature1.feature[i] * feature2.feature[i];
    }
    return (1 - sum) * 2;
}

float FaceFeatureExtractor::GetScore(float distance) {
    std::vector<float> scores = GetScoreLevel();
    if (scores.size() < 3) {
        LOG_WARN("{}", "Score Level Invalid");
        return 0.f;
    }

    float score = 0;
    if (distance < 0.333f * scores[0]) {
        score = 100.0f;
    }
    if (distance >= 0.333f * scores[0] && distance < scores[0]) {
        score = ((1.5f * (scores[0] - distance) / scores[0] * 0.2f + 0.8f) * 100.f);
    }
    if ((distance >= scores[0]) && (distance <= scores[1])) {
        score = ((0.8f - (distance - scores[0]) / ((scores[1] - scores[0])) * 0.2f) * 100.f);
    }
    if ((distance > scores[1]) && (distance <= scores[2])) {
        score = (((scores[2] - distance) / (scores[2] - scores[1]) * 0.6f) * 100.f);
    }
    if (distance > scores[2]) {
        score = 0;
    }

    return score;
}

std::vector<float> FaceFeatureExtractor::GetScoreLevel() {
    // Online safety: FaceLib::SearchFeature may call GetScore/GetScoreLevel before service thread Init,
    // must guarantee no crash. If not ready, try synchronous Init; return empty array on failure.
    if (!is_ready_) {
        ServiceEnable();
        if (!Init()) {
            return {};
        }
    }

    std::lock_guard<std::shared_mutex> lock(mtx_);
    if (!is_ready_ || !recog_inst_) {
        return {};
    }
    return recog_inst_->GetScoreLevel();
}

bool FaceFeatureExtractor::HandMsgQulityAngle(VideoFramePtr frame, MsgGetFeaturesFeature& feature,
                                              std::vector<AiDetectRstEl>& inputFaces, float quality) {
    auto ret = FaceQulityAngle(frame, inputFaces);
    if (!ret) {
        LOG_WARN("{}", "QulitityAngle Failed!");
        feature.code = feature_code::kQuality;
        return false;
    }

    for (auto& face : inputFaces) {
        for (auto& claRst : face.classifyRst) {
            LOG_INFO("{}:{}", claRst.label, claRst.confidence);
        }
    }
    return GetQualityAngle(inputFaces[0].classifyRst, feature, quality);
}

bool FaceFeatureExtractor::HandMsgMask(VideoFramePtr frame, MsgGetFeaturesFeature& /*feature*/,
                                       std::vector<AiDetectRstEl>& inputFaces) {
    auto ret = FaceMask(frame, inputFaces);
    if (!ret) {
        LOG_WARN("{}", "Mask Failed!");
        return true;
    }

    LOG_WARN("{}", "Mask detection check!");
    return true;
}

bool FaceFeatureExtractor::HandMsgFeature(VideoFramePtr frame, MsgGetFeaturesFeature& feature,
                                          std::vector<AiDetectRstEl>& inputFaces) {
    if (inputFaces.size() < 1) {
        LOG_WARN("{}", "NO Faces!");
        feature.code = feature_code::kNoFace;
        return false;
    }
    auto ret = FaceLandmark(frame, inputFaces);
    if (!ret || inputFaces[0].landmark.landmark.empty()) {
        LOG_WARN("{}", "LandMark Failed!");
        feature.code = feature_code::kAiFailed;
        return false;
    }
    ret = FaceFeature(frame, inputFaces);
    if (!ret || inputFaces[0].feature.feature.empty()) {
        LOG_WARN("{}", "FaceFeature Failed!");
        feature.code = feature_code::kAiFailed;
        return false;
    }

    feature.code = feature_code::kSuccess;

    auto features = inputFaces[0].feature;
    util::LtonConvert(features.feature.data(), features.feature.size());
    feature.feature = util::EncBase64Ex(reinterpret_cast<uint8_t*>(features.feature.data()),
                                        features.feature.size() * sizeof(float));
    return true;
}

bool FaceFeatureExtractor::HandFeature(VideoFramePtr frame, AiFeature& feature,
                                       std::vector<AiDetectRstEl>& inputFaces) {
    if (inputFaces.size() < 1) {
        LOG_WARN("{}", "NO Faces!");
        return false;
    }
    auto ret = FaceLandmark(frame, inputFaces);
    if (!ret || inputFaces[0].landmark.landmark.empty()) {
        LOG_WARN("{}", "LandMark Failed!");
        return false;
    }

    ret = FaceFeature(frame, inputFaces);
    if (!ret || inputFaces[0].feature.feature.empty()) {
        LOG_WARN("{}", "FaceFeature Failed!");
        return false;
    }
    feature = inputFaces[0].feature;
    return true;
}

std::error_condition FaceFeatureExtractor::HandFeatureImage(VideoFramePtr& image, float quality,
                                                            AiFeature& aifeature, VideoFramePtr& cutImage) {
    MsgGetFeaturesFeature feature;
    constexpr int kMaxInitRetries = 30;
    // First request may arrive before background model init; wait up to 3 seconds
    for (int i = 0; i < kMaxInitRetries; ++i) {
        {
            std::shared_lock<std::shared_mutex> lock(mtx_);
            if (is_ready_) {
                break;
            }
        }
        std::this_thread::sleep_for(timing::kMediumPollInterval);
    }
    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        if (!is_ready_) {
            LOG_WARN("{}HandFeatureImage aborted: service initializing timeout", kTag);
            return util::ErrorEnum::ServiceNotInit;
        }
    }

    if (!VideoFrameValid(image)) {
        LOG_ERRO("{}", "Decode Jpeg Failed!");
        return util::ErrorEnum::InternalError;
    }
    // AiDetector/AiClassifier/AiRecognizer assume BGR input; convert I420 to BGR to prevent misdetection
    auto work_image = image;
    if (work_image->GetPixelFormat() == media::PixelFormat::PIXEL_I420) {
        auto bgr =
            service::ServiceRegistry::Instance().Get<service::IVideoFrameTransform>().I4202BGR(work_image);
        if (!VideoFrameValid(bgr)) {
            LOG_ERRO("{}I4202BGR failed before face pipeline", kTag);
            return util::ErrorEnum::InternalError;
        }
        work_image = bgr;
    } else if (work_image->GetPixelFormat() != media::PixelFormat::PIXEL_BGR8 &&
               work_image->GetPixelFormat() != media::PixelFormat::PIXEL_RGB8) {
        LOG_ERRO("{}unsupported pixel format for face pipeline, fmt:{}", kTag,
                 static_cast<int>(work_image->GetPixelFormat()));
        return util::ErrorEnum::InvalidParam;
    }
    LOG_INFO("{}HandFeatureImage begin, image:{}x{}, quality_thres:{}", kTag, image->GetWidth(),
             image->GetHeight(), quality);

    std::vector<AiDetectRstEl> faces;
    // Face detection
    if (!HandMsgFaceDetect(work_image, feature, faces)) {
        LOG_ERRO("{}Detect Face Failed, code:{}, rect_count:{}", kTag, feature.code, feature.rects.size());
        if (feature.code == feature_code::kNoAiInst) {
            return util::ErrorEnum::ServiceNotInit;
        }
        return util::ErrorEnum::NoFaceDetected;
    }

    // If the detected face is not in the center of the photo, skip feature extraction
    util::Box det_bounding_box;
    det_bounding_box.x      = faces[0].box.x;
    det_bounding_box.y      = faces[0].box.y;
    det_bounding_box.width  = faces[0].box.width;
    det_bounding_box.height = faces[0].box.height;
    LOG_INFO("{}Face bbox validated x:{} y:{} w:{} h:{}", kTag, det_bounding_box.x, det_bounding_box.y,
             det_bounding_box.width, det_bounding_box.height);

    if ((det_bounding_box & util::Box{0, 0, static_cast<int>(work_image->GetWidth()),
                                      static_cast<int>(work_image->GetHeight())}) != det_bounding_box) {
        LOG_ERRO("{}person is incomplete, image:{}x{}, box x:{} y:{} w:{} h:{}", kTag, image->GetWidth(),
                 image->GetHeight(), det_bounding_box.x, det_bounding_box.y, det_bounding_box.width,
                 det_bounding_box.height);
        return util::ErrorEnum::FaceIsIncomplete;
    }

    // Extend the bounding box by factor 2
    TargetScalerParam ext_param;
    ext_param.scale_north = 2.0f;
    ext_param.scale_south = 2.0f;
    ext_param.scale_east  = 2.0f;
    ext_param.scale_west  = 2.0f;
    auto rect             = DoScaleBox(det_bounding_box, ext_param, static_cast<int>(work_image->GetWidth()),
                                       static_cast<int>(work_image->GetHeight()));
    int center_x          = static_cast<int>(work_image->GetWidth() / 2);
    LOG_INFO("{}Scaled bbox x:{} y:{} w:{} h:{}, center_x:{}", kTag, rect.x, rect.y, rect.width, rect.height,
             center_x);

    if (rect.x <= center_x && rect.x + rect.width >= center_x) {
        constexpr int kMinFaceSize = 100;
        if (det_bounding_box.width < kMinFaceSize || det_bounding_box.height < kMinFaceSize) {
            LOG_ERRO("face is too small {} {}", det_bounding_box.width, det_bounding_box.height);
            return util::ErrorEnum::FaceIsTooSmall;
        }

        // Quality and angle check
        if (!HandMsgQulityAngle(work_image, feature, faces, quality)) {
            LOG_ERRO("{}Get Face Quality and Angle Failed, code:{}, quality_thres:{}", kTag, feature.code,
                     quality);
            return util::ErrorEnum::QualityNotEnough;
        }

        if (!HandFeature(work_image, aifeature, faces)) {
            LOG_ERRO("{}Get Face feature Failed!", kTag);
            return util::ErrorEnum::GetFeatureFailed;
        }
        LOG_INFO("{}HandFeatureImage success, feature_len:{}", kTag, aifeature.feature.size());

        TargetScalerParam param;
        param.scale_north = 2.0f;
        param.scale_south = 2.0f;
        param.scale_east  = 2.0f;
        param.scale_west  = 2.0f;
        auto scle         = DoScaleBox(det_bounding_box, param, static_cast<int>(image->GetWidth()),
                                       static_cast<int>(image->GetHeight()));

        util::Box crop_box;
        crop_box.x      = scle.x;
        crop_box.y      = scle.y;
        crop_box.width  = scle.width;
        crop_box.height = scle.height;

        cutImage =
            service::ServiceRegistry::Instance().Get<service::IVideoFrameTransform>().Crop(image, crop_box);
        return util::ErrorEnum::Success;
    }
    LOG_ERRO("{}person is not in center, scaled box x:{} w:{}, center_x:{}", kTag, rect.x, rect.width,
             center_x);
    return util::ErrorEnum::FaceIsNotInTheMiddle;
}

MsgGetFeaturesFeature FaceFeatureExtractor::HandMsgImage(const MsgGetFeaturesImage& image, float quality) {
    MsgGetFeaturesFeature feature;
    feature.imageId = image.imageId;
    // Base64 decode
    auto pic_bin = std::move(util::DecBase64Vec(image.imageBase64));
    if (0 == pic_bin.size()) {
        LOG_ERRO("{}", "Recv Base64 Pic data error!");
        feature.code = feature_code::kBase64;
        return feature;
    }

    // JPEG decode
    auto image_data =
        service::ServiceRegistry::Instance().Get<service::IVideoFrameCodec>().DecodeJpeg(pic_bin);
    if (!VideoFrameValid(image_data)) {
        LOG_ERRO("{}", "Decode Jpeg Failed!");
        feature.code = feature_code::kMem;
        return feature;
    }

    std::vector<AiDetectRstEl> faces;
    // Face detection
    if (!HandMsgFaceDetect(image_data, feature, faces)) {
        return feature;
    }

    // Quality and angle check
    if (!HandMsgQulityAngle(image_data, feature, faces, quality)) {
        return feature;
    }

    // If wearing a mask, return early
    if (!HandMsgMask(image_data, feature, faces)) {
        return feature;
    }

    HandMsgFeature(image_data, feature, faces);
    return feature;
}

MsgGetFeaturesSend FaceFeatureExtractor::HandMsg(const MsgGetFeaturesRecv& data, std::error_condition& errc) {
    MsgGetFeaturesSend msg;
    if (!is_ready_) {
        errc = util::ErrorEnum::ServiceNotInit;  // Service not started
        return msg;
    }
    float quality = default_face_quality_;
    if (data.quality >= 0) {
        quality = data.quality;
    }
    for (auto& image : data.imageList) {
        msg.features.push_back(HandMsgImage(image, quality));
    }

    return msg;
}

}  // namespace cosmo
