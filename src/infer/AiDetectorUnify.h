// AiDetectorUnify — Unified detector model wrapper.

#pragma once

#include <memory>

#include "infer/AiCommon.h"
#include "infer/AiComponment.h"
#include "media/VideoFrame.h"
#include "nn/utils/default_component.h"

namespace cosmo {
class AiDetectorUnify {
public:
    AiDetectorUnify(const std::string& atomic_code, const std::string& json_path,
                    const std::string& model_path);
    ~AiDetectorUnify();

    util::ErrorEnum Init();

    util::ErrorEnum Detect(const std::vector<VideoFramePtr>& images, std::vector<AiConfidence> conf_thres,
                           std::vector<std::vector<AiDetectRstEl>>& results);

    std::vector<std::string> GetLabels() {
        return labels_;
    }

    util::ErrorEnum GetMaxBatchSize(size_t& value);

private:
    util::ErrorEnum Forward(const std::vector<VideoFramePtr>& images,
                            std::vector<std::vector<AiDetectRstEl>>& results);

    size_t max_batch_size_ = 1;
    std::string atomic_code_;
    std::string cfg_path_;
    std::string model_path_;
    std::vector<std::string> labels_;
    std::unique_ptr<cosmo::nn::DefaultComponent> detector_;
    AppProfiler profiler_;
};

using AiDetectorUnifyPtr = std::shared_ptr<AiDetectorUnify>;
}  // namespace cosmo
