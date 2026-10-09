#pragma once

#include "nn/pipeline/model_pipeline.h"

namespace cosmo::nn {
class LayaRunner;
class LayaPipeline : public ModelPipeline {
public:
    LayaPipeline();
    ~LayaPipeline() override;
    Status Init(const PipelineConfig& config, const std::string& model_path, DeviceType device_type,
                int device_id, IProfiler* profiler, const std::string& tokenizer_path,
                const std::string& word_table_path, bool use_skip) override;
    Status Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) override;
    Status PrepareText(const std::string& input, std::string& output) override;
    int GetMaxBatchSize() const override {
        return 1;
    }
    std::string GetModelType() const override {
        return "laya_v";
    }
    OutputCategory GetOutputCategory() const override {
        return OutputCategory::TEXT;
    }
    Status ParseTextOutput(std::vector<std::vector<std::string>>& outputs) override;

private:
#ifdef COSMO_NN_USE_SOPHON_BACKEND
    std::unique_ptr<LayaRunner> runner_;
#endif
};
}  // namespace cosmo::nn
