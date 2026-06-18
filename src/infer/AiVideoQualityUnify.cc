// AiVideoQualityUnify.cc — Video quality analysis wrapper implementation.

#include "infer/AiVideoQualityUnify.h"

#include "infer/AiComponment.h"
#include "util/Log.h"

namespace cosmo::infer {

constexpr float kDefaultBlurThreshold      = 0.1f;
constexpr float kDefaultSnowThreshold      = 0.5f;
constexpr float kDefaultStripeThreshold    = 0.025f;
constexpr float kDefaultBrightnessLow      = 0.196f;
constexpr float kDefaultBrightnessHigh     = 0.78f;
constexpr float kDefaultOcclusionThreshold = 0.4f;
constexpr float kDefaultContrastThreshold  = 0.8f;
constexpr float kDefaultDeviationThreshold = 1.0f;

AiVideoQualityUnify::AiVideoQualityUnify() {
    LOG_INFO("{}", "AiVideoQualityUnify");
}

AiVideoQualityUnify::~AiVideoQualityUnify() {
    LOG_INFO("{}", "AiVideoQualityUnify Delete");
}

util::ErrorEnum AiVideoQualityUnify::Init(AiVideoQualityType type) {
    if (inst_) {
        LOG_WARN("{}", "AiVideoQualityUnify Init SDK Inst Failed. Already Init");
        return util::ErrorEnum::Created;
    }

    type_ = type;
    inst_ = std::make_shared<cosmo::nn::VideoQualityUtils>(GetDeviceType());

    switch (type_) {
        case AiVideoQualityType::kBlur: {
            if (threshold_ < 0.0f) {
                threshold_ = kDefaultBlurThreshold;
            }
            auto status = inst_->AllocBlurAnalysisExtraMemory();
            if (!bool(status)) {
                LOG_WARN("AllocBlurAnalysisExtraMemory failed:{}", status.description());
                return util::ErrorEnum::Failed;
            }
        } break;
        case AiVideoQualityType::kSnow: {
            if (threshold_ < 0.0f) {
                threshold_ = kDefaultSnowThreshold;
            }
            auto status = inst_->AllocSnowAnalysisExtraMemory();
            if (!bool(status)) {
                LOG_WARN("AllocSnowAnalysisExtraMemory failed:{}", status.description());
                return util::ErrorEnum::Failed;
            }
        } break;
        case AiVideoQualityType::kStripe: {
            if (threshold_ < 0.0f) {
                threshold_ = kDefaultStripeThreshold;
            }
            auto status = inst_->AllocStripeAnalysisExtraMemory();
            if (!bool(status)) {
                LOG_WARN("AllocStripeAnalysisExtraMemory failed:{}", status.description());
                return util::ErrorEnum::Failed;
            }
        } break;
        case AiVideoQualityType::kBrightness: {
            if (threshold_ < 0.0f) {
                threshold_ = kDefaultBrightnessLow;
            }
            if (threshold_ext_ < 0.0f) {
                threshold_ext_ = kDefaultBrightnessHigh;
            }
            auto status = inst_->AllocBrightnessAnalysisExtraMemory();
            if (!bool(status)) {
                LOG_WARN("AllocBrightnessAnalysisExtraMemory failed:{}", status.description());
                return util::ErrorEnum::Failed;
            }
        } break;
        case AiVideoQualityType::kOcclusion: {
            if (threshold_ < 0.0f) {
                threshold_ = kDefaultOcclusionThreshold;
            }
            auto status = inst_->AllocOcclusionAnalysisExtraMemory();
            if (!bool(status)) {
                LOG_WARN("AllocOcclusionAnalysisExtraMemory failed:{}", status.description());
                return util::ErrorEnum::Failed;
            }
        } break;
        case AiVideoQualityType::kContrast: {
            if (threshold_ < 0.0f) {
                threshold_ = kDefaultContrastThreshold;
            }
            auto status = inst_->AllocContrastAnalysisExtraMemory();
            if (!bool(status)) {
                LOG_WARN("AllocContrastAnalysisExtraMemory failed:{}", status.description());
                return util::ErrorEnum::Failed;
            }
        } break;
        case AiVideoQualityType::kDeviation: {
            if (threshold_ < 0.0f) {
                threshold_ = kDefaultDeviationThreshold;
            }
            auto status = inst_->AllocDeviationAnalysisExtraMemory();
            if (!bool(status)) {
                LOG_WARN("AllocDeviationAnalysisExtraMemory failed:{}", status.description());
                return util::ErrorEnum::Failed;
            }
        } break;
        default: {
            LOG_WARN("type_:{} Not Support", static_cast<int>(type_));
            return util::ErrorEnum::Failed;
        }
    }

    LOG_INFO("{}", "AiVideoQualityUnify Inst Init");
    is_init_ = true;
    return util::ErrorEnum::Success;
}

bool AiVideoQualityUnify::Analysis(const ::VideoFramePtr& image, float& score) {
    if (!inst_) {
        LOG_WARN("{}", "AiVideoQualityUnify SDK Inst Not Init");
        return false;
    }

    if (!is_init_) {
        LOG_WARN("{}", "AiVideoQualityUnify SDK Not Init");
        return false;
    }

    auto blob = ConvertImageToBlob(image);

    float ret{-1.0f};
    cosmo::nn::Status status;
    switch (type_) {
        case AiVideoQualityType::kBlur: {
            // Only clarity has a range of 0-100
            status = inst_->BlurAnalysis(blob, &ret);
            if (!bool(status)) {
                LOG_WARN("BlurAnalysis failed:{}", status.description());
                return false;
            }
            score = ret / 100.0f;

            return (score < threshold_);
        } break;
        case AiVideoQualityType::kSnow: {
            status = inst_->SnowAnalysis(blob, &ret);
            if (!bool(status)) {
                LOG_WARN("SnowAnalysis failed:{}", status.description());
                return false;
            }
            score = ret;

            return (score > threshold_);
        } break;
        case AiVideoQualityType::kStripe: {
            status = inst_->StripeAnalysis(blob, &ret);
            if (!bool(status)) {
                LOG_WARN("StripeAnalysis failed:{}", status.description());
                return false;
            }
            score = ret;

            return (score > threshold_);
        } break;
        case AiVideoQualityType::kBrightness: {
            status = inst_->BrightnessAnalysis(blob, &ret);
            if (!bool(status)) {
                LOG_WARN("BrightnessAnalysis failed:{}", status.description());
                return false;
            }
            score = ret;

            return ((score < threshold_) || (score > threshold_ext_));
        } break;
        case AiVideoQualityType::kOcclusion: {
            status = inst_->OcclusionAnalysis(blob, &ret);
            if (!bool(status)) {
                LOG_WARN("OcclusionAnalysis failed:{}", status.description());
                return false;
            }
            score = ret;

            return (score > threshold_);
        } break;
        case AiVideoQualityType::kContrast: {
            status = inst_->ContrastAnalysis(blob, &ret);
            if (!bool(status)) {
                LOG_WARN("ContrastAnalysis failed:{}", status.description());
                return false;
            }
            score = ret;

            return (score > threshold_);
        } break;
        case AiVideoQualityType::kDeviation: {
            status = inst_->DeviationAnalysis(blob, &ret);
            if (!bool(status)) {
                LOG_WARN("DeviationAnalysis failed:{}", status.description());
                return false;
            }
            score = ret;

            return (score > threshold_);
        } break;
        default: {
            LOG_WARN("type_:{} Not Support", static_cast<int>(type_));
            return false;
        }
    }

    return false;
}

void AiVideoQualityUnify::SetThreshold(float threshold, float threshold_ext) {
    if (threshold >= 0.0f) {
        threshold_ = threshold;
    }
    if (threshold_ext >= 0.0f) {
        threshold_ext_ = threshold_ext;
    }
}

}  // namespace cosmo::infer