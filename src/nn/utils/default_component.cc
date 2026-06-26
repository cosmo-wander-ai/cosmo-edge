#include "nn/utils/default_component.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "nn/pipeline/model_pipeline.h"
#include "nn/pipeline/pipeline_utils.h"

namespace cosmo::nn {

std::mutex DefaultComponent::mutex;

// ─── Constructor: parse JSON → create Pipeline → Init ────

DefaultComponent::DefaultComponent(std::string json_path, std::string model_path, DeviceType device_type,
                                   IProfiler* profiler, std::string tokenizer_path,
                                   std::string word_table_path, int device_id, bool use_skip) {
    std::unique_lock<std::mutex> lock(mutex);

    std::string json_content;
    auto status = ModelInfoUtils::LoadJson(json_path, json_content);
    if (!bool(status))
        throw std::invalid_argument(status.description());

    PipelineConfig config;
    status = pipeline_utils::ParsePipelineConfig(json_content, config);
    if (!bool(status))
        throw std::invalid_argument("Failed to parse pipeline config: " + std::string(status.description()));

    auto& registry = ModelPipelineRegistry::Instance();
    if (!registry.Has(config.model_type)) {
        std::string registered;
        for (auto& t : registry.GetRegisteredTypes()) {
            if (!registered.empty())
                registered += ", ";
            registered += t;
        }
        throw std::invalid_argument("Unknown model_type: " + config.model_type +
                                    ". Registered: " + registered);
    }

    pipeline_.reset(registry.Create(config.model_type));
    if (!pipeline_)
        throw std::runtime_error("Failed to create pipeline for: " + config.model_type);

    status = pipeline_->Init(config, model_path, device_type, device_id, profiler, tokenizer_path,
                             word_table_path, use_skip);
    if (!bool(status))
        throw std::runtime_error("Pipeline init failed: " + std::string(status.description()));
}

DefaultComponent::~DefaultComponent() = default;

// ─── All methods delegate directly to Pipeline ───────────

Status DefaultComponent::Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) {
    if (inputs.size() == 0)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "No input");
    return pipeline_->Forward(inputs);
}

int DefaultComponent::GetMaxBatchSize() const {
    return pipeline_->GetMaxBatchSize();
}

Status DefaultComponent::SetThreshold(int id, float threshold) {
    return pipeline_->SetThreshold(id, threshold);
}

Status DefaultComponent::SetThreshold(std::string label, float threshold) {
    return pipeline_->SetThreshold(label, threshold);
}

Status DefaultComponent::GetThreshold(int id, float* threshold) {
    return pipeline_->GetThreshold(id, threshold);
}

Status DefaultComponent::GetThreshold(std::string label, float* threshold) {
    return pipeline_->GetThreshold(label, threshold);
}

Status DefaultComponent::GetCategoryInfo(int class_id, CategoryInfo& info) {
    return pipeline_->GetCategoryInfo(class_id, info);
}

Status DefaultComponent::GetCategoryInfo(std::string label, CategoryInfo& info) {
    return pipeline_->GetCategoryInfo(label, info);
}

std::vector<float> DefaultComponent::GetScoreLevels() const {
    return pipeline_->GetScoreLevels();
}

std::vector<std::string> DefaultComponent::GetSelectedClassnames() const {
    return pipeline_->GetSelectedClassnames();
}

// ─── ParseOutput template specializations ────────────────

template <typename T>
Status DefaultComponent::ParseOutput(std::vector<std::vector<T>>& outputs) {
    static_assert(std::is_same<T, uint8_t>() || std::is_same<T, float>() || std::is_same<T, char>() ||
                      std::is_same<T, ObjectInfoV1>() || std::is_same<T, std::string>(),
                  "Only support float/char/ObjectInfoV1/std::string/uint8_t");
    return Status(COSMO_NN_ERR_INVALID_INPUT, "Unsupported output type");
}

template <>
Status DefaultComponent::ParseOutput<ObjectInfoV1>(std::vector<std::vector<ObjectInfoV1>>& outputs) {
    auto cat = pipeline_->GetOutputCategory();
    switch (cat) {
        case OutputCategory::DETECTION:
            return pipeline_->ParseDetectionOutput(outputs);
        case OutputCategory::CLASSIFICATION:
            return pipeline_->ParseClassifyOutput(outputs);
        case OutputCategory::KEYPOINTS:
            return pipeline_->ParseKeypointsOutput(outputs);
        default:
            return Status(COSMO_NN_ERR_NET, "Unsupported model type for ObjectInfoV1 output");
    }
}

template <>
Status DefaultComponent::ParseOutput<float>(std::vector<std::vector<float>>& outputs) {
    return pipeline_->ParseFeatureOutput(outputs);
}

template <>
Status DefaultComponent::ParseOutput<uint8_t>(std::vector<std::vector<uint8_t>>& outputs) {
    return pipeline_->ParseSegmentationOutput(outputs);
}

template <>
Status DefaultComponent::ParseOutput<std::string>(std::vector<std::vector<std::string>>& outputs) {
    return pipeline_->ParseTextOutput(outputs);
}

template <>
Status DefaultComponent::ParseOutput<char>(std::vector<std::vector<char>>& outputs) {
    return pipeline_->ParseOcrOutput(outputs);
}

}  // namespace cosmo::nn
