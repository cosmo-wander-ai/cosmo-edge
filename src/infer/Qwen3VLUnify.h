#pragma once

// Qwen3VLUnify.h — Qwen3 vision-language model wrapper.

#include <memory>
#include <string>
#include <vector>

#include "infer/AiCommon.h"
#include "infer/AiComponment.h"
#include "media/VideoFrame.h"
#include "nn/utils/default_component.h"

namespace cosmo {
struct Qwen3VLGenerationParam {
    bool do_sample{true};
    int top_k{20};
    float top_p{0.8f};
    float temperature{0.7f};
};

struct Qwen3VLResult {
    std::string text;
    int64_t frame_index{-1};
    int64_t timestamp{-1};
};

class Qwen3VLUnify {
public:
    Qwen3VLUnify(const std::string& atomic_code, const std::string& json_path, const std::string& model_path,
                 const std::string& tokenizer_path);
    ~Qwen3VLUnify();

    util::ErrorEnum Init();

    // Generate text from images with prompt and generation parameters
    util::ErrorEnum Generate(const std::vector<VideoFramePtr>& images,
                             const std::vector<std::string>& prompts, const Qwen3VLGenerationParam& gen_param,
                             std::vector<Qwen3VLResult>& results);

    util::ErrorEnum GetMaxBatchSize(size_t* value) const;

private:
    util::ErrorEnum Forward(const std::vector<VideoFramePtr>& images, const std::vector<std::string>& prompts,
                            const Qwen3VLGenerationParam& gen_param, std::vector<Qwen3VLResult>& results);

private:
    size_t max_batch_size_{1};
    std::string atomic_code_;
    std::string cfg_path_;
    std::string model_path_;
    std::string tokenizer_path_;
    std::unique_ptr<cosmo::nn::DefaultComponent> generator_;
    AppProfiler profiler_;
};

using Qwen3VLUnifyPtr = std::shared_ptr<Qwen3VLUnify>;
}  // namespace cosmo
