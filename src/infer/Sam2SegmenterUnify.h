#pragma once

// Sam2SegmenterUnify.h — SAM2 segmentation model wrapper.

#include <memory>
#include <string>
#include <vector>

#include "infer/AiCommon.h"
#include "infer/AiComponment.h"
#include "media/VideoFrame.h"
#include "nn/utils/default_component.h"

namespace cosmo {
enum class Sam2InputType { BOX = 0, POINT = 1 };

struct Sam2PromptInput {
    Sam2InputType input_type{Sam2InputType::BOX};
    std::vector<float> coords;  // Point or box coordinates
    std::vector<float> labels;  // Labels
};

class Sam2SegmenterUnify {
public:
    Sam2SegmenterUnify(const std::string& atomicCode, const std::string& jsonPath,
                       const std::string& modelPath);
    ~Sam2SegmenterUnify();

    util::ErrorEnum Init();

    // SAM2 segmentation with point or box prompts
    util::ErrorEnum Segment(const std::vector<VideoFramePtr>& images,
                            const std::vector<Sam2PromptInput>& prompts, std::vector<AiDetectRstEl>& results);

    util::ErrorEnum GetMaxBatchSize(size_t& value) const;

private:
    util::ErrorEnum Forward(const std::vector<VideoFramePtr>& images,
                            const std::vector<Sam2PromptInput>& prompts, std::vector<AiDetectRstEl>& results);

private:
    size_t max_batch_size_ = 1;
    std::string atomic_code_;
    std::string cfg_path_;
    std::string model_path_;
    std::unique_ptr<cosmo::nn::DefaultComponent> segmenter_;
    AppProfiler profiler_;
};

using Sam2SegmenterUnifyPtr = std::shared_ptr<Sam2SegmenterUnify>;
}  // namespace cosmo
