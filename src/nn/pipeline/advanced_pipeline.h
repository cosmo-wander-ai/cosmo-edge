#pragma once

#include "nn/pipeline/model_pipeline.h"

namespace cosmo::nn {

class SAM2Pipeline : public ModelPipeline {
public:
    Status Init(const PipelineConfig& config, const std::string& model_path, DeviceType device_type,
                int device_id, IProfiler* profiler, const std::string& tokenizer_path,
                const std::string& word_table_path, bool use_skip) override;
    Status Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) override;
    int GetMaxBatchSize() const override {
        return max_batch_;
    }
    std::string GetModelType() const override {
        return "SAM2";
    }
    OutputCategory GetOutputCategory() const override {
        return OutputCategory::SEGMENTATION;
    }
    Status ParseSegmentationOutput(std::vector<std::vector<uint8_t>>& outputs) override;

private:
    int max_batch_ = 1;
};

class DinoPipeline : public ModelPipeline {
public:
    Status Init(const PipelineConfig& config, const std::string& model_path, DeviceType device_type,
                int device_id, IProfiler* profiler, const std::string& tokenizer_path,
                const std::string& word_table_path, bool use_skip) override;
    Status Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) override;
    int GetMaxBatchSize() const override {
        return max_batch_;
    }
    std::string GetModelType() const override {
        return "dino";
    }
    OutputCategory GetOutputCategory() const override {
        return OutputCategory::DETECTION;
    }
    Status ParseDetectionOutput(std::vector<std::vector<ObjectInfoV1>>& outputs) override;

private:
    int max_batch_ = 1;
};

class Qwen3VLPipeline : public ModelPipeline {
public:
    ~Qwen3VLPipeline() override;
    Status Init(const PipelineConfig& config, const std::string& model_path, DeviceType device_type,
                int device_id, IProfiler* profiler, const std::string& tokenizer_path,
                const std::string& word_table_path, bool use_skip) override;
    Status Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) override;
    int GetMaxBatchSize() const override;
    std::string GetModelType() const override {
        return "qwen3vl";
    }
    OutputCategory GetOutputCategory() const override {
        return OutputCategory::TEXT;
    }
    Status ParseTextOutput(std::vector<std::vector<std::string>>& outputs) override;

private:
    void* runner_ = nullptr;
};

class Qwen3_5Pipeline : public ModelPipeline {
public:
    ~Qwen3_5Pipeline() override;
    Status Init(const PipelineConfig& config, const std::string& model_path, DeviceType device_type,
                int device_id, IProfiler* profiler, const std::string& tokenizer_path,
                const std::string& word_table_path, bool use_skip) override;
    Status Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) override;
    int GetMaxBatchSize() const override;
    std::string GetModelType() const override {
        return "qwen3_5";
    }
    OutputCategory GetOutputCategory() const override {
        return OutputCategory::TEXT;
    }
    Status ParseTextOutput(std::vector<std::vector<std::string>>& outputs) override;

private:
    void* runner_ = nullptr;
};

class SegmentationPipeline : public ModelPipeline {
public:
    Status Init(const PipelineConfig& config, const std::string& model_path, DeviceType device_type,
                int device_id, IProfiler* profiler, const std::string& tokenizer_path,
                const std::string& word_table_path, bool use_skip) override;
    Status Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) override;
    int GetMaxBatchSize() const override {
        return max_batch_;
    }
    std::string GetModelType() const override {
        return "segmentation";
    }
    OutputCategory GetOutputCategory() const override {
        return OutputCategory::SEGMENTATION;
    }
    Status ParseSegmentationOutput(std::vector<std::vector<uint8_t>>& outputs) override;

private:
    int max_batch_ = 1;
};

class OcrPipeline : public ModelPipeline {
public:
    Status Init(const PipelineConfig& config, const std::string& model_path, DeviceType device_type,
                int device_id, IProfiler* profiler, const std::string& tokenizer_path,
                const std::string& word_table_path, bool use_skip) override;
    Status Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) override;
    int GetMaxBatchSize() const override {
        return max_batch_;
    }
    std::string GetModelType() const override {
        return "ocr";
    }
    OutputCategory GetOutputCategory() const override {
        return OutputCategory::RAW;
    }
    Status ParseOcrOutput(std::vector<std::vector<char>>& outputs) override;

private:
    int max_batch_ = 1;
};

}  // namespace cosmo::nn
