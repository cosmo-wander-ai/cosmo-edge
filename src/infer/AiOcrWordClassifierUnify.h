// AiOcrWordClassifierUnify.h — OCR word classifier model wrapper.

#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "infer/AiCommon.h"
#include "infer/AiComponment.h"
#include "media/VideoFrame.h"
#include "nn/utils/default_component.h"

namespace cosmo {

class AiOcrWordClassifierUnify {
public:
    AiOcrWordClassifierUnify(const std::string& alg_code, const std::string& json_path,
                             const std::string& model_path, const std::string& word_dict_path);
    ~AiOcrWordClassifierUnify();

    [[nodiscard]] util::ErrorEnum Init();

    // Output classification label
    [[nodiscard]] util::ErrorEnum Classify(const VideoFramePtr& image, const util::Box& box,
                                           std::string& rst);

    [[nodiscard]] util::ErrorEnum Classify(const std::vector<VideoFramePtr>& images,
                                           const std::vector<util::Box>& boxes,
                                           std::vector<std::string>& rsts);

    [[nodiscard]] util::ErrorEnum Classify(const std::vector<VideoFramePtr>& images,
                                           const std::vector<std::vector<util::Box>>& boxes,
                                           std::vector<std::vector<std::string>>& rsts);

    [[nodiscard]] util::ErrorEnum GetMaxBatchSize(size_t& value) const;

    [[nodiscard]] util::ErrorEnum WarpAffine(std::shared_ptr<cosmo::nn::Blob> blob,
                                             std::vector<std::pair<float, float>>& src_points,
                                             std::shared_ptr<cosmo::nn::Blob>& out_blob);

    [[nodiscard]] util::ErrorEnum WarpAffine(const VideoFramePtr& in_image, const AiLandmarkData& landmark,
                                             const VideoFramePtr& out_image);

private:
    [[nodiscard]] util::ErrorEnum Forward(const std::vector<VideoFramePtr>& images,
                                          const std::vector<std::vector<std::vector<int>>>& datas,
                                          std::vector<std::vector<char>>& outputs);

    std::string json_path_;
    std::string model_path_;
    std::string word_dict_path_;
    size_t max_batch_size_{1};
    std::string atomic_code_;
    std::unique_ptr<cosmo::nn::DefaultComponent> classifier_;
    AppProfiler profiler_;
};

using AiOcrWordClassifierUnifyPtr = std::shared_ptr<AiOcrWordClassifierUnify>;

}  // namespace cosmo
