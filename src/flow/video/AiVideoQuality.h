// AiVideoQuality header.

#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "flow/action/AlgActionBase.h"

namespace cosmo {

enum class AiVideoQualityType {
    kBlur = 0,
    kSnow,
    kStripe,
    kBrightness,
    kOcclusion,
    kContrast,
    kDeviation,
    kMax,
};

constexpr bool IsValidVideoQualityType(int value) {
    return value >= static_cast<int>(AiVideoQualityType::kBlur) &&
           value < static_cast<int>(AiVideoQualityType::kDeviation);
}

struct AiVideoQualityParam {
    AiVideoQualityType type{AiVideoQualityType::kBlur};
    float threshold{-1.0f};
    float threshold_ext{-1.0f};
};

class AiVideoQuality : public AlgActionBase {
public:
    AiVideoQuality(const std::string& task, ActionNode& action_param);
    ~AiVideoQuality() override;

    AiVideoQuality(const AiVideoQuality&)            = delete;
    AiVideoQuality& operator=(const AiVideoQuality&) = delete;

    [[nodiscard]] bool Start() override;
    [[nodiscard]] bool AiSdkInit();

    bool AnalysisKey(const MsgDynamicKeyValue& param);
    bool ModifyParam(const std::string& channel_id, const std::string& task_id,
                     std::vector<MsgDynamicKeyValue>& params) override;
    bool SetParam(const std::string& channel_id, const std::string& task_id,
                  std::vector<MsgDynamicKeyValue>& params) override;

    bool SetArea(const std::string& channel_id, const std::string& task_id, std::vector<MsgTaskArea>& areas,
                 std::vector<MsgTaskArea>& shielded_areas) override;

    // Get overlay info
    MsgOverviewMem GetOverviewInfo(const std::string& channel_id, const std::string& task_id,
                                   int64_t stream_index = -1, int64_t from = -1, int64_t to = -1) override;

private:
    void HandFrame(AlgDataPtr alg_data) override;

    AiVideoQualityParam params_;
    std::atomic<bool> unsupported_reported_{false};
};
using AiVideoQualityPtr = std::shared_ptr<AiVideoQuality>;
}  // namespace cosmo
