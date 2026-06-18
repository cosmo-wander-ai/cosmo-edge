// AiVideoQualityUnify.h — Video quality analysis wrapper (blur, noise, brightness, etc.).

#pragma once

#include <memory>

#include "infer/AiCommon.h"
#include "media/VideoFrame.h"
#include "nn/utils/model_info_utils.h"
#include "nn/utils/net_utils.h"
#include "nn/utils/video_quality_utils.h"
#include "util/ErrorCode.h"

namespace cosmo::infer {

enum class AiVideoQualityType {
    kBlur = 0,    // Blur detection          threshold: 10, below = blurry
    kSnow,        // Snow noise detection    threshold: 0.5, above = snow noise present
    kStripe,      // Stripe/interlace        threshold gpu:0.025, cpu:0.25, above = stripe noise
    kBrightness,  // Brightness anomaly      low:0.196, high:0.78
                  // below low or above high = too dark/bright (reported as brightness anomaly)
    kOcclusion,   // Occlusion detection     threshold: 0.4, above = occluded
    kContrast,    // Contrast detection      threshold: 0.8, above = abnormal
    kDeviation,   // Deviation detection     multi-area action judgement
                  // areaLimitTargetCount/areaLimitTargetType as algorithm-level params
    kMax          //
};

constexpr bool IsValidVideoQualityType(int value) {
    return (value >= static_cast<int>(AiVideoQualityType::kBlur)) &&
           (value < static_cast<int>(AiVideoQualityType::kDeviation));
}

class AiVideoQualityUnify {
public:
    AiVideoQualityUnify();
    ~AiVideoQualityUnify();

    [[nodiscard]] util::ErrorEnum Init(AiVideoQualityType type);

    [[nodiscard]] bool Analysis(const ::VideoFramePtr& image, float& score);

    void SetThreshold(float threshold, float threshold_ext = -1.0f);

private:
    AiVideoQualityType type_{AiVideoQualityType::kBlur};
    float threshold_{-1.0f};      // Brightness low threshold / other algorithm thresholds
    float threshold_ext_{-1.0f};  // Brightness high threshold
    bool is_init_{false};
    std::shared_ptr<cosmo::nn::VideoQualityUtils> inst_{nullptr};
};

using AiVideoQualityUnifyPtr = std::shared_ptr<AiVideoQualityUnify>;
}  // namespace cosmo::infer
