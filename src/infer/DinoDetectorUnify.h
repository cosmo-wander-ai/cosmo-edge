// DinoDetectorUnify.h — DINO visual detection model wrapper.

#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "infer/AiCommon.h"
#include "infer/AiComponment.h"
#include "media/VideoFrame.h"
#include "nn/utils/default_component.h"

namespace cosmo {
class DinoDetectorUnify {
public:
    DinoDetectorUnify(const std::string& atomicCode, const std::string& jsonPath,
                      const std::string& modelPath, const std::string& vocabPath);
    ~DinoDetectorUnify();

    util::ErrorEnum Init();

    // Detect with a text prompt, returns detection results
    util::ErrorEnum Detect(const std::vector<VideoFramePtr>& images, const std::string& prompt,
                           std::vector<std::vector<AiDetectRstEl>>& results);

    std::vector<std::string> GetLabels();

    util::ErrorEnum GetMaxBatchSize(size_t& value);

private:
    util::ErrorEnum Forward(const std::vector<VideoFramePtr>& images, const std::string& prompt,
                            std::vector<std::vector<AiDetectRstEl>>& results);

private:
    size_t max_batch_size_ = 1;
    std::string atomic_code_;
    std::string cfg_path_;
    std::string model_path_;
    std::string vocab_path_;
    std::vector<std::string> labels_;
    std::unique_ptr<cosmo::nn::DefaultComponent> detector_;
    std::map<std::string, int> vocab_;  // token -> id, loaded from vocab.txt
    AppProfiler profiler_;
};

using DinoDetectorUnifyPtr = std::shared_ptr<DinoDetectorUnify>;
}  // namespace cosmo
