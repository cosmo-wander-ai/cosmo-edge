// AiDetectInterface — Detection model interface.

#pragma once

#include "infer/AiDetectorUnify.h"
#include "infer/InferPoolTypes.h"

namespace cosmo::infer {
class AiDetectInterface {
public:
    AiDetectInterface(DetectorPoolPtr pool, const std::string& alg_code, const std::string& cfg_path,
                      const std::string& model_path);
    ~AiDetectInterface();

    AiDetectInterface(const AiDetectInterface&)            = delete;
    AiDetectInterface& operator=(const AiDetectInterface&) = delete;

    [[nodiscard]] util::ErrorEnum Detect(const VideoFramePtr& image,
                                         const std::vector<AiConfidence>& conf_thres,
                                         std::vector<AiDetectRstEl>& rst) const;

private:
    std::string alg_code_;
    std::string cfg_path_;
    std::string model_path_;
    DetectorPoolPtr reuse_obj_{nullptr};
};

using AiDetectInterfacePtr = std::shared_ptr<AiDetectInterface>;

}  // namespace cosmo::infer
