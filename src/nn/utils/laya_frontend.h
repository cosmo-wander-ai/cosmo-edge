#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace cosmo::nn::laya {

inline constexpr int kSequence   = 256;
inline constexpr int kHidden     = 768;
inline constexpr int kImageSlots = 130;
inline constexpr int kVocabulary = 256000;

struct Question {
    int type          = 0;
    float temperature = 1;
    nlohmann::json temperature_info;
    std::vector<std::string> labels;
    std::vector<int32_t> tokens;
    std::vector<int> markers;
    size_t image_position = 0;
};

using Encoder = std::function<std::vector<int32_t>(const std::string&)>;
// Translate serialization-only differences for the repository's HF tokenizer.
// Vocabulary, merge order, normalization and byte fallback remain unchanged.
std::string TokenizerJson(nlohmann::json config);
// Throws on unsupported or over-budget input; never silently truncates a question.
Question CompileQuestion(const nlohmann::ordered_json& question, const nlohmann::json& config,
                         const Encoder& encode, const std::string& text_state = {});
void AttentionMasks(size_t valid_length, std::vector<float>& full, std::vector<float>& sliding);
// BGR host pixels -> centered white square -> bilinear RGB 256x256 -> raster patches.
std::vector<float> ImagePatches(const uint8_t* bgr, int width, int height, size_t stride);

struct Tensor {
    std::vector<size_t> shape;
    std::vector<float> data;
};
Tensor ReadFloatNpy(const std::string& path);

class EmbeddingTable {
public:
    explicit EmbeddingTable(const std::string& path);
    ~EmbeddingTable();
    EmbeddingTable(const EmbeddingTable&)            = delete;
    EmbeddingTable& operator=(const EmbeddingTable&) = delete;
    std::vector<float> Splice(const Question& question, const std::vector<float>& visual) const;

private:
    void* mapping_          = nullptr;
    size_t mapped_bytes_    = 0;
    const uint16_t* values_ = nullptr;
};

struct Scores {
    std::vector<float> logits;
    std::vector<float> action_logits;
    std::vector<float> probabilities;
    size_t top   = 0;
    float margin = 0;
};
using Heads = std::map<std::string, Tensor>;
Heads ReadHeads(const std::string& directory);
Scores Score(const std::vector<float>& hidden, const Question& question, const Heads& heads);

}  // namespace cosmo::nn::laya
