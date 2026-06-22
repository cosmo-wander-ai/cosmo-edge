// AiClassifierUnify — Unified classifier model wrapper.

#pragma once

#include <memory>

#include "infer/AiCommon.h"
#include "infer/AiComponment.h"
#include "media/VideoFrame.h"
#include "nn/utils/default_component.h"

namespace cosmo {
class AiClassifierUnify {
public:
    AiClassifierUnify(const std::string& atomic_code, const std::string& json_path,
                      const std::string& model_path);
    ~AiClassifierUnify();

    AiClassifierUnify(const AiClassifierUnify&)            = delete;
    AiClassifierUnify& operator=(const AiClassifierUnify&) = delete;

    util::ErrorEnum Init();

    util::ErrorEnum Classify(const VideoFramePtr& image, std::vector<AiDetectRstEl>& io_rst,
                             bool use_box = true);

    util::ErrorEnum Classify(const std::vector<VideoFramePtr>& images, std::vector<AiDetectRstEl>& io_rst,
                             bool use_box = true);

    util::ErrorEnum ClassifyMultSub(const std::vector<VideoFramePtr>& images,
                                    std::vector<std::vector<AiDetectRstEl>>& io_rst, const bool& use_mult_sub,
                                    bool use_box = true);

    util::ErrorEnum Classify(const std::vector<VideoFramePtr>& images,
                             std::vector<std::vector<AiDetectRstEl>>& io_rst, bool use_box = true);

    util::ErrorEnum Classify(const VideoFramePtr& image_base, const VideoFramePtr& image,
                             AiDetectRstEl& io_rst);

    util::ErrorEnum Classify(const std::vector<VideoFramePtr>& images_base,
                             const std::vector<VideoFramePtr>& images, std::vector<AiDetectRstEl>& io_rst);

    [[nodiscard]] util::ErrorEnum GetMaxBatchSize(size_t& value) const;

    [[nodiscard]] util::ErrorEnum GetAttributeCategory(AiDetectRstEl& io_rst);

    [[nodiscard]] util::ErrorEnum GetAttributeCategory(std::vector<AiDetectRstEl>& io_rst);

private:
    util::ErrorEnum Forward(const std::vector<VideoFramePtr>& images,
                            const std::vector<std::vector<std::vector<int>>>& datas,
                            std::vector<std::vector<cosmo::nn::ObjectInfoV1>>& outputs, bool use_box);

    std::vector<int> CollectBoxOrLandmarkData(const AiDetectRstEl& io_el, bool use_box);
    void AppendClassifyResults(AiDetectRstEl& io_el, const std::vector<cosmo::nn::ObjectInfoV1>& obj);
    void DispatchBatchResults(const std::vector<std::vector<cosmo::nn::ObjectInfoV1>>& outputs,
                              const std::vector<std::pair<size_t, size_t>>& indexes,
                              std::vector<std::vector<AiDetectRstEl>>& io_rst);

private:
    size_t max_batch_size_ = 1;
    std::string atomic_code_;
    std::string cfg_path_;
    std::string model_path_;
    std::unique_ptr<cosmo::nn::DefaultComponent> classifier_;
    AppProfiler profiler_;
};

using AiClassifierUnifyPtr = std::shared_ptr<AiClassifierUnify>;
}  // namespace cosmo
