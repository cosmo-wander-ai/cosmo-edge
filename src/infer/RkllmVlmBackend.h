#pragma once

#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "infer/AiCommon.h"
#include "media/VideoFrame.h"
#include "util/ErrorCode.h"

namespace cosmo {

struct Qwen3VLGenerationParam;
struct Qwen3VLResult;

class RkllmVlmBackend {
public:
    explicit RkllmVlmBackend(std::string model_path);
    ~RkllmVlmBackend();

    RkllmVlmBackend(const RkllmVlmBackend&)            = delete;
    RkllmVlmBackend& operator=(const RkllmVlmBackend&) = delete;

    // Call before Init; omitted settings preserve the short business judgement budget.
    util::ErrorEnum ConfigureGeneration(int max_new_tokens, int context_length);
    struct EvaluationOptions {
        int max_new_tokens{256};
        int context_length{2048};
        // Reference observation only; platform token counts need not match.
        int expected_input_tokens{0};
        std::string vision_normalization;
    };
    util::ErrorEnum ConfigureEvaluation(const EvaluationOptions& options);
    nlohmann::json GetEvaluationMetadata() const;
    nlohmann::json Evaluate(const VideoFramePtr& image, const std::string& prompt,
                            const std::string& input_dump_prefix = "");
    nlohmann::json EvaluateRgb(const uint8_t* pixels, size_t bytes, int width, int height,
                               const std::string& prompt, const std::string& input_dump_prefix = "");
    int AbortEvaluation();
    int IsEvaluationRunning() const;
    util::ErrorEnum Init();
    util::ErrorEnum Generate(const std::vector<VideoFramePtr>& images,
                             const std::vector<std::string>& prompts, const Qwen3VLGenerationParam& gen_param,
                             std::vector<Qwen3VLResult>& results);

private:
    nlohmann::json EvaluatePacked(const uint8_t* pixels, size_t bytes, int width, int height, bool bgr,
                                  const std::string& prompt, const std::string& input_dump_prefix);
    struct Impl;
    Impl* impl_{nullptr};
    std::string model_path_;
    int max_new_tokens_{2};
    int context_length_{512};
    bool evaluation_enabled_{false};
    EvaluationOptions evaluation_options_;
};

}  // namespace cosmo
