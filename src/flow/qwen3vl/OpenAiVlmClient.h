#pragma once

#include <string>
#include <vector>

#include "infer/Qwen3VLUnify.h"
#include "media/VideoFrame.h"
#include "util/ErrorCode.h"

namespace cosmo {

struct OpenAiVlmConfig {
    std::string provider{"local_model"};
    std::string base_url;
    std::string api_key;
    std::string model;
    std::string endpoint{"/chat/completions"};
    int timeout_ms{60000};
    int max_tokens{256};
    float temperature{0.7f};
    float top_p{0.8f};

    bool Enabled() const {
        return provider == "openai_vlm" || provider == "openai" || provider == "openai_compatible";
    }
};

class OpenAiVlmClient {
public:
    static util::ErrorEnum Generate(const OpenAiVlmConfig& config, const std::vector<VideoFramePtr>& images,
                                    const std::vector<std::string>& prompts,
                                    const Qwen3VLGenerationParam& gen_param,
                                    std::vector<Qwen3VLResult>& results);
};

}  // namespace cosmo
