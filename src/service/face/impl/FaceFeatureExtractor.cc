// FaceFeatureExtractor — FaceFeatureExtractor — Face feature extraction service.

#include "service/face/impl/FaceFeatureExtractor.h"

#include <chrono>
#include <thread>

#include "media/PixelFormat.h"
#include "service/detail/ServiceRegistry.h"
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

FaceFeatureExtractor::~FaceFeatureExtractor() {
    LOG_INFO("{}[{}] Stop", kTag, name_);
    is_running_ = false;
    stop();
    LOG_INFO("{}[{}] Delete", kTag, name_);
}

FaceFeatureExtractor::FaceFeatureExtractor()
    : Thread("FaceFeature Service"), name_("FaceFeature Service"), is_running_(true) {
    if (!start()) {
        is_running_ = false;
        LOG_ERRO("{}[{}] Init failed: worker thread is still joinable", kTag, name_);
    }
    LOG_INFO("{}[{}] Init ", kTag, name_);
}

void FaceFeatureExtractor::ServiceEnable() {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    if (!is_service_enabled_) {
        LOG_INFO("{}[{}] Service Enable ", kTag, name_);
        is_service_enabled_ = true;
    }
}

void FaceFeatureExtractor::run() {
    bool service_enable = false;
    while (is_running_) {
        std::this_thread::sleep_for(timing::kOneSecondInterval);

        if (is_service_enabled_ != service_enable) {
            LOG_INFO("{}[{}] service switch changed: {} -> {}", kTag, name_, service_enable,
                     is_service_enabled_);
            if (is_service_enabled_) {
                if (Init()) {
                    service_enable = true;
                    LOG_INFO("{}[{}] service enable success, is_ready_:{}", kTag, name_, is_ready_);
                } else {
                    LOG_ERRO("{}[{}] service enable failed, is_ready_:{}", kTag, name_, is_ready_);
                }
            } else {
                if (Destroy()) {
                    service_enable = false;
                    LOG_INFO("{}[{}] service disable success, is_ready_:{}", kTag, name_, is_ready_);
                }
            }
        }
    }
    LOG_INFO("{} Quit", Name());
}

template <typename T>
bool FaceFeatureExtractor::InitModel(std::shared_ptr<T>& instance, const std::string& algCode,
                                     const char* modelLabel) {
    if (instance) {
        return true;
    }
    std::string cfg_path;
    std::string model_path;
    auto cfg_ret = service::ServiceRegistry::Instance().Get<service::IModelService>().GetModelCfg(
        algCode, cfg_path, model_path);
    if (!cfg_ret) {
        LOG_WARN("{}[{}] Get Model Configure Failed. AlgCode:{} ret:{}", kTag, name_, algCode, cfg_ret);
        return false;
    }
    LOG_INFO("{}[{}] {} model cfg ok. AlgCode:{} cfg_path:{} model_path:{}", kTag, name_, modelLabel, algCode,
             cfg_path, model_path);
    instance = std::make_shared<T>(algCode, cfg_path, model_path);
    auto ret = instance->Init();
    if (util::ErrorEnum::Success != ret) {
        instance.reset();
        instance = nullptr;
        LOG_WARN("{}[{}] {} Sdk Init Failed Get Ret {}", kTag, name_, algCode, ret);
        return false;
    }
    LOG_INFO("{}[{}] {} init success. AlgCode:{}", kTag, name_, modelLabel, algCode);

    return true;
}

bool FaceFeatureExtractor::Destroy() {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    if (!is_ready_) {
        LOG_INFO("{}[{}] Not Ready ", kTag, name_);
        return true;
    }
    is_ready_ = false;
    // Face detection
    if (face_det_inst_) {
        face_det_inst_.reset();
        face_det_inst_ = nullptr;
    }
    // Face quality/angle
    if (face_quality_inst_) {
        face_quality_inst_.reset();
        face_quality_inst_ = nullptr;
    }
    // Mask classification
    if (face_mask_inst_) {
        face_mask_inst_.reset();
        face_mask_inst_ = nullptr;
    }
    // Landmark positions
    if (landmark_inst_) {
        landmark_inst_.reset();
        landmark_inst_ = nullptr;
    }
    // Feature extraction
    if (recog_inst_) {
        recog_inst_.reset();
        recog_inst_ = nullptr;
    }

    return true;
}

bool FaceFeatureExtractor::Init() {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    if (is_ready_) {
        LOG_INFO("{}[{}] Ready ", kTag, name_);
        return true;
    }
    util::DurationLogger duration_logger("FaceFeatures Model Init");

    constexpr const char* kAlgCodeDetect    = "1000001";
    constexpr const char* kAlgCodeQuality   = "1000012";
    constexpr const char* kAlgCodeMask      = "1000010";
    constexpr const char* kAlgCodeLandmark  = "1000016";
    constexpr const char* kAlgCodeRecognize = "1000005";

    // Face detection
    if (!InitModel<AiDetectorUnify>(face_det_inst_, kAlgCodeDetect, "Detector")) {
        LOG_ERRO("{}[{}] InitModel<Detector> failed. algCode:1000001", kTag, name_);
        return false;
    }
    // Face quality/angle
    if (!InitModel<AiClassifierUnify>(face_quality_inst_, kAlgCodeQuality, "Classifier")) {
        LOG_ERRO("{}[{}] InitModel<Classifier> failed. algCode:1000012", kTag, name_);
        return false;
    }
    LOG_WARN("{}", "Mask Have No Model");
    // Mask classification (not yet available)
    if (!InitModel<AiClassifierUnify>(face_mask_inst_, kAlgCodeMask, "Classifier")) {
        LOG_ERRO("{}[{}] InitModel<Classifier> failed. algCode:1000010", kTag, name_);
        return false;
    }
    // Landmark positions
    if (!InitModel<AiLandmarkerUnify>(landmark_inst_, kAlgCodeLandmark, "Landmark")) {
        LOG_ERRO("{}[{}] InitModel<Landmark> failed. algCode:1000016", kTag, name_);
        return false;
    }
    // Feature extraction
    if (!InitModel<AiRecognizerUnify>(recog_inst_, kAlgCodeRecognize, "Recognizer")) {
        LOG_ERRO("{}[{}] InitModel<Recognizer> failed. algCode:1000005", kTag, name_);
        return false;
    }
    is_ready_ = true;
    LOG_INFO("{}[{}] Init all face models success, is_ready_:{}", kTag, name_, is_ready_);
    return true;
}

std::vector<AiDetectRstEl> FaceFeatureExtractor::FaceDetect(VideoFramePtr frame, bool& bInited) {
    std::vector<AiDetectRstEl> rst;

    std::lock_guard<std::shared_mutex> lock(mtx_);
    if (!is_ready_) {
        LOG_WARN("{}FaceDetect skipped: service not ready", kTag);
        bInited = false;
        return rst;
    }

    if (!face_det_inst_) {
        LOG_WARN("{}FaceDetect skipped: face_det_inst_ is null", kTag);
        bInited = false;
        return rst;
    }
    bInited = true;
    std::vector<VideoFramePtr> images;
    images.push_back(frame);
    std::vector<AiConfidence> conf_thres;
    std::vector<std::vector<AiDetectRstEl>> rsts;
    face_det_inst_->Detect(images, conf_thres, rsts);
    if (rsts.empty()) {
        return rst;
    }

    return rsts[0];
}

bool FaceFeatureExtractor::FaceQulityAngle(VideoFramePtr frame, std::vector<AiDetectRstEl>& ioPuts) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    if (!is_ready_) {
        return false;
    }
    if (!face_quality_inst_) {
        return false;
    }
    auto ret = face_quality_inst_->Classify(frame, ioPuts);
    if (util::ErrorEnum::Success != ret) {
        return false;
    }
    return true;
}

bool FaceFeatureExtractor::FaceMask(VideoFramePtr frame, std::vector<AiDetectRstEl>& ioPuts) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    if (!is_ready_) {
        return false;
    }
    if (!face_mask_inst_) {
        return false;
    }
    auto ret = face_mask_inst_->Classify(frame, ioPuts);
    if (util::ErrorEnum::Success != ret) {
        return false;
    }
    return true;
}

bool FaceFeatureExtractor::FaceLandmark(VideoFramePtr frame, std::vector<AiDetectRstEl>& ioPuts) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    if (!is_ready_) {
        return false;
    }
    if (!landmark_inst_) {
        return false;
    }
    auto ret = landmark_inst_->Marker(frame, ioPuts);
    if (util::ErrorEnum::Success != ret) {
        return false;
    }
    return true;
}

// Database search and feature comparison — moved to FaceFeatureExtractorSearch.cc

}  // namespace cosmo
