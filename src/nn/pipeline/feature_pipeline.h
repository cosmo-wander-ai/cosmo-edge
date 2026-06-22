#pragma once

#include "nn/pipeline/model_pipeline.h"

namespace cosmo::nn {

class FeaturePipeline : public ModelPipeline {
public:
    Status Init(const PipelineConfig& config, const std::string& model_path, DeviceType device_type,
                int device_id, IProfiler* profiler, const std::string& tokenizer_path,
                const std::string& word_table_path, bool use_skip) override;

    Status Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) override;

    int GetMaxBatchSize() const override {
        return max_batch_;
    }
    std::string GetModelType() const override {
        return "feature";
    }
    OutputCategory GetOutputCategory() const override {
        return OutputCategory::FEATURE;
    }

    Status ParseFeatureOutput(std::vector<std::vector<float>>& outputs) override;

private:
    int max_batch_ = 1;
};

}  // namespace cosmo::nn
