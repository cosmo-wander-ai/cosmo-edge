#pragma once

#include <memory>
#include <string>
#include <vector>

#include "nn/core/status.h"

namespace cosmo::nn {

class Blob;

/**
 * Qwen3VL inference runner, implemented within the CWNN framework, works with DefaultComponent.
 * Input: images (list of image blobs), prompts (list of prompt blobs, each storing UTF-8 text).
 * Output: one generated text per image, retrieved via GetTextOutputs().
 */
class Qwen3VLRunner {
public:
    Qwen3VLRunner() = default;
    ~Qwen3VLRunner();

    Qwen3VLRunner(const Qwen3VLRunner&)            = delete;
    Qwen3VLRunner& operator=(const Qwen3VLRunner&) = delete;

    /**
     * Initialize: load bmodel and tokenizer files; generation params and do_sample are read from
     * model_config_json_path config.generation section.
     * @param model_path   Path to bmodel file
     * @param tokenizer_path Full path to tokenizer.json
     * @param device_id    Device id
     * @param model_config_json_path Model config JSON path (e.g. config_qwen3vl.json), reads
     * config.generation.do_sample and generation parameters from it
     */
    Status Init(const std::string& model_path, const std::string& tokenizer_path, int device_id = 0,
                const std::string& model_config_json_path = "", const std::string& model_type = "qwen3vl");

    /**
     * Run inference. Input format matches DefaultComponent::Forward: { images, prompts }.
     * @param images  One blob per image (NHWC image data, device or host)
     * @param prompts One blob per prompt (stores prompt text bytes, host)
     */
    Status Run(const std::vector<std::vector<std::shared_ptr<Blob>>>& inputs);

    /**
     * Get text outputs from the last Run. One string per image.
     */
    const std::vector<std::vector<std::string>>& GetTextOutputs() const {
        return text_outputs_;
    }

    int GetMaxBatchSize() const {
        return max_batch_size_;
    }

private:
    struct Impl;

    template <typename Model>
    static Status RunImpl(Model& model, Impl& impl,
                          const std::vector<std::vector<std::shared_ptr<Blob>>>& inputs,
                          std::vector<std::vector<std::string>>& text_outputs);

    std::unique_ptr<Impl> impl_;
    std::vector<std::vector<std::string>> text_outputs_;
    int max_batch_size_ = 1;
};

}  // namespace cosmo::nn
