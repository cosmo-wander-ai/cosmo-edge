// AiLandmarkerUnify — Unified landmark model wrapper.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "infer/AiCommon.h"
#include "infer/AiComponment.h"
#include "media/VideoFrame.h"
#include "nn/utils/default_component.h"

namespace cosmo {
class AiLandmarkerUnify {
public:
    AiLandmarkerUnify(const std::string& jsonPath, const std::string& modelPath);

    /// 3-arg constructor for InstancePool<T,PTR> template compatibility.
    /// The atomicCode is unused — pool-level keying is handled by InferPoolServiceImpl.
    AiLandmarkerUnify(const std::string& /*atomicCode*/, const std::string& jsonPath,
                      const std::string& modelPath)
        : AiLandmarkerUnify(jsonPath, modelPath) {}
    ~AiLandmarkerUnify();

    util::ErrorEnum Init();

    util::ErrorEnum Marker(VideoFramePtr image, std::vector<AiDetectRstEl>& ioRst);

    util::ErrorEnum Marker(const std::vector<VideoFramePtr>& images, std::vector<AiDetectRstEl>& ioRst);

    util::ErrorEnum Marker(const std::vector<VideoFramePtr>& images,
                           std::vector<std::vector<AiDetectRstEl>>& ioRst);

    util::ErrorEnum GetMaxBatchSize(size_t& value);

private:
    util::ErrorEnum Forward(const std::vector<VideoFramePtr>& images,
                            const std::vector<std::vector<std::vector<int>>>& rects,
                            std::vector<std::vector<cosmo::nn::ObjectInfoV1>>& outputs);

    std::vector<int> CollectRectData(const AiDetectRstEl& ioEl);
    void DispatchLandmarkResults(const std::vector<std::vector<cosmo::nn::ObjectInfoV1>>& outputs,
                                 const std::vector<std::pair<int, int>>& indexes,
                                 std::vector<std::vector<AiDetectRstEl>>& ioRst);

    size_t max_batch_size_{1};
    std::string cfg_path_;
    std::string model_path_;
    std::unique_ptr<cosmo::nn::DefaultComponent> landmarker_;
    AppProfiler profiler_;
};

using AiLandmarkerUnifyPtr = std::shared_ptr<AiLandmarkerUnify>;
}  // namespace cosmo
