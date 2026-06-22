#pragma once

#include "nn/pipeline/model_pipeline.h"

namespace cosmo::nn {

class YoloV5DetPipeline : public ModelPipeline {
public:
    Status Init(const PipelineConfig& config, const std::string& model_path, DeviceType device_type,
                int device_id, IProfiler* profiler, const std::string& tokenizer_path,
                const std::string& word_table_path, bool use_skip) override;

    Status Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) override;

    int GetMaxBatchSize() const override {
        return max_batch_;
    }
    std::string GetModelType() const override {
        return "yolov5_det";
    }
    OutputCategory GetOutputCategory() const override {
        return OutputCategory::DETECTION;
    }

    Status ParseDetectionOutput(std::vector<std::vector<ObjectInfoV1>>& outputs) override;

private:
    int max_batch_ = 1;
};

class YoloV8DetPipeline : public ModelPipeline {
public:
    Status Init(const PipelineConfig& config, const std::string& model_path, DeviceType device_type,
                int device_id, IProfiler* profiler, const std::string& tokenizer_path,
                const std::string& word_table_path, bool use_skip) override;

    Status Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) override;

    int GetMaxBatchSize() const override {
        return max_batch_;
    }
    std::string GetModelType() const override {
        return "yolov8_det";
    }
    OutputCategory GetOutputCategory() const override {
        return OutputCategory::DETECTION;
    }

    Status ParseDetectionOutput(std::vector<std::vector<ObjectInfoV1>>& outputs) override;

private:
    int max_batch_ = 1;
};

class Yolo26DetPipeline : public ModelPipeline {
public:
    Status Init(const PipelineConfig& config, const std::string& model_path, DeviceType device_type,
                int device_id, IProfiler* profiler, const std::string& tokenizer_path,
                const std::string& word_table_path, bool use_skip) override;

    Status Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) override;

    int GetMaxBatchSize() const override {
        return max_batch_;
    }
    std::string GetModelType() const override {
        return "yolo26_det";
    }
    OutputCategory GetOutputCategory() const override {
        return OutputCategory::DETECTION;
    }

    Status ParseDetectionOutput(std::vector<std::vector<ObjectInfoV1>>& outputs) override;

private:
    int max_batch_ = 1;
};

class GenericDetectorPipeline : public ModelPipeline {
public:
    Status Init(const PipelineConfig& config, const std::string& model_path, DeviceType device_type,
                int device_id, IProfiler* profiler, const std::string& tokenizer_path,
                const std::string& word_table_path, bool use_skip) override;

    Status Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) override;

    int GetMaxBatchSize() const override {
        return max_batch_;
    }
    std::string GetModelType() const override {
        return "detector";
    }
    OutputCategory GetOutputCategory() const override {
        return OutputCategory::DETECTION;
    }

    Status ParseDetectionOutput(std::vector<std::vector<ObjectInfoV1>>& outputs) override;

private:
    int max_batch_ = 1;
};

}  // namespace cosmo::nn
