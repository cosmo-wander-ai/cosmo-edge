#pragma once

#include "nn/pipeline/model_pipeline.h"

namespace cosmo::nn {

class KeypointsPipeline : public ModelPipeline {
public:
    Status Init(const PipelineConfig& config, const std::string& model_path, DeviceType device_type,
                int device_id, IProfiler* profiler, const std::string& tokenizer_path,
                const std::string& word_table_path) override;

    Status Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) override;

    int GetMaxBatchSize() const override {
        return max_batch_;
    }
    std::string GetModelType() const override {
        return "keypoints";
    }
    OutputCategory GetOutputCategory() const override {
        return OutputCategory::KEYPOINTS;
    }

    Status ParseKeypointsOutput(std::vector<std::vector<ObjectInfoV1>>& outputs) override;

private:
    int max_batch_            = 1;
    bool allow_fixed_padding_ = false;
    bool output_ready_        = false;
    DimsVector declared_output_shape_;
};

}  // namespace cosmo::nn
