// AiRecognizerUnify — Unified recognizer model wrapper.

#pragma once

#include <memory>

#include "infer/AiCommon.h"
#include "infer/AiComponment.h"
#include "media/VideoFrame.h"
#include "nn/utils/default_component.h"

namespace cosmo {
class AiRecognizerUnify {
public:
    AiRecognizerUnify(const std::string& json_path, const std::string& model_path);

    /// 3-arg constructor for InstancePool<T,PTR> template compatibility.
    /// The atomicCode is unused — pool-level keying is handled by InferPoolServiceImpl.
    AiRecognizerUnify(const std::string& /*atomic_code*/, const std::string& json_path,
                      const std::string& model_path)
        : AiRecognizerUnify(json_path, model_path) {}
    ~AiRecognizerUnify();

    util::ErrorEnum Init();

    util::ErrorEnum Extract(VideoFramePtr image, std::vector<AiDetectRstEl>& io_rst, bool use_box = false);

    util::ErrorEnum Extract(const std::vector<VideoFramePtr>& images, std::vector<AiDetectRstEl>& io_rst,
                            bool use_box = false);

    util::ErrorEnum Extract(const std::vector<VideoFramePtr>& images,
                            std::vector<std::vector<AiDetectRstEl>>& io_rst, bool use_box = false);

    std::vector<float> GetScoreLevel();

    float CompareFeature(const AiFeature& feature1, const AiFeature& feature2);

    util::ErrorEnum GetMaxBatchSize(size_t& value);

private:
    util::ErrorEnum Forward(const std::vector<VideoFramePtr>& images,
                            const std::vector<std::vector<std::vector<int>>>& datas,
                            std::vector<std::vector<float>>& features, bool use_box);

private:
    size_t max_batch_size_ = 1;
    std::string cfg_path_;
    std::string model_path_;
    std::unique_ptr<cosmo::nn::DefaultComponent> recognizer_;
    AppProfiler profiler_;
};

using AiRecognizerUnifyPtr = std::shared_ptr<AiRecognizerUnify>;
}  // namespace cosmo
