#include "nn/pipeline/model_pipeline.h"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <numeric>
#include <unordered_set>

#include "nn/core/graph.h"
#include "nn/utils/model_info_utils.h"

namespace cosmo::nn {

// ─── Pipeline Base Class Default Implementation ─────────────────────

ModelPipeline::ModelPipeline() = default;

ModelPipeline::~ModelPipeline() {
    // unique_ptr members (graph_, Op pointers in model_info_) auto-destruct
}

Status ModelPipeline::ParseDetectionOutput(std::vector<std::vector<ObjectInfoV1>>& outputs) {
    return Status(COSMO_NN_ERR_NET, "Unsupported output type for this pipeline");
}
Status ModelPipeline::ParseDinoDetectionOutput(std::vector<std::vector<ObjectInfoV1>>& outputs,
                                               float text_threshold, float box_threshold) {
    (void)outputs;
    (void)text_threshold;
    (void)box_threshold;
    return Status(COSMO_NN_ERR_NET, "Pipeline does not support GroundingDINO thresholds");
}
Status ModelPipeline::ParseClassifyOutput(std::vector<std::vector<ObjectInfoV1>>& outputs) {
    return Status(COSMO_NN_ERR_NET, "Unsupported output type for this pipeline");
}
Status ModelPipeline::ParseFeatureOutput(std::vector<std::vector<float>>& outputs) {
    return Status(COSMO_NN_ERR_NET, "Unsupported output type for this pipeline");
}
Status ModelPipeline::ParseKeypointsOutput(std::vector<std::vector<ObjectInfoV1>>& outputs) {
    return Status(COSMO_NN_ERR_NET, "Unsupported output type for this pipeline");
}
Status ModelPipeline::ParseSegmentationOutput(std::vector<std::vector<uint8_t>>& outputs) {
    return Status(COSMO_NN_ERR_NET, "Unsupported output type for this pipeline");
}
Status ModelPipeline::ParseTextOutput(std::vector<std::vector<std::string>>& outputs) {
    return Status(COSMO_NN_ERR_NET, "Unsupported output type for this pipeline");
}
Status ModelPipeline::ParseOcrOutput(std::vector<std::vector<char>>& outputs) {
    return Status(COSMO_NN_ERR_NET, "Unsupported output type for this pipeline");
}

// ─── Threshold / Label Management ──────────────────────────────────

Status ModelPipeline::SetThreshold(int id, float threshold) {
    auto iter = std::find(selected_indices_.begin(), selected_indices_.end(), id);
    if (iter == selected_indices_.end())
        return Status(COSMO_NN_ERR_INVALID_INPUT, "No such id");
    selected_thresholds_[std::distance(selected_indices_.begin(), iter)] = threshold;
    return Status{COSMO_NN_OK};
}

Status ModelPipeline::SetThreshold(const std::string& label, float threshold) {
    auto iter = std::find(selected_classnames_.begin(), selected_classnames_.end(), label);
    if (iter == selected_classnames_.end())
        return Status(COSMO_NN_ERR_INVALID_INPUT, "No such label");
    selected_thresholds_[std::distance(selected_classnames_.begin(), iter)] = threshold;
    return Status{COSMO_NN_OK};
}

Status ModelPipeline::GetThreshold(int id, float* threshold) {
    if (!threshold)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "Threshold pointer is null");
    auto iter = std::find(selected_indices_.begin(), selected_indices_.end(), id);
    if (iter == selected_indices_.end())
        return Status(COSMO_NN_ERR_INVALID_INPUT, "No such id");
    *threshold = selected_thresholds_[std::distance(selected_indices_.begin(), iter)];
    return Status{COSMO_NN_OK};
}

Status ModelPipeline::GetThreshold(const std::string& label, float* threshold) {
    if (!threshold)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "Threshold pointer is null");
    auto iter = std::find(selected_classnames_.begin(), selected_classnames_.end(), label);
    if (iter == selected_classnames_.end())
        return Status(COSMO_NN_ERR_INVALID_INPUT, "No such label");
    *threshold = selected_thresholds_[std::distance(selected_classnames_.begin(), iter)];
    return Status{COSMO_NN_OK};
}

Status ModelPipeline::GetCategoryInfo(int class_id, CategoryInfo& info) {
    auto& instructions = model_info_.config.instructions;
    if (instructions.empty())
        return Status(COSMO_NN_ERR_INVALID_INPUT, "No instruction");

    auto& categories = instructions.at(0).categories;
    if (categories.empty())
        return Status(COSMO_NN_ERR_INVALID_INPUT, "No category info");

    const int total_splits = std::accumulate(categories.begin(), categories.end(), 0,
                                             [](int total, const auto& c) { return total + c.split; });

    if (class_id < 0 || class_id >= total_splits)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "Invalid class id");

    int id = class_id;
    for (const auto& c : categories) {
        if (id - c.split < 0) {
            info = c;
            break;
        }
        id -= c.split;
    }
    return Status{COSMO_NN_OK};
}

Status ModelPipeline::GetCategoryInfo(const std::string& label, CategoryInfo& info) {
    const auto& instructions = model_info_.config.instructions;
    if (instructions.empty())
        return Status(COSMO_NN_ERR_INVALID_INPUT, "No instruction");

    const auto& infos = instructions.at(0).infos;
    int index         = -1;
    for (int i = 0; i < static_cast<int>(infos.size()); i++) {
        if (infos[i].class_name == label) {
            index = i;
            break;
        }
    }
    if (index == -1)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "No such label");
    return GetCategoryInfo(index, info);
}

std::vector<float> ModelPipeline::GetScoreLevels() const {
    return model_info_.config.face_info.score_level;
}

std::vector<std::string> ModelPipeline::GetSelectedClassnames() const {
    return selected_classnames_;
}

// ─── Graph Lifecycle ───────────────────────────────────

Status ModelPipeline::InitGraph(const std::string& model_path, DeviceType device_type, int device_id,
                                IProfiler* profiler, const std::string& tokenizer_path) {
    graph_ = std::make_unique<Graph>();
    if (profiler)
        graph_->SetProfiler(profiler);

    num_models_ = static_cast<int>(model_info_.models.size());

    std::string mp = model_path;
    RETURN_ON_FAIL(graph_->Init(model_info_, mp, device_type, tokenizer_path, device_id));
    return Status{COSMO_NN_OK};
}

Status ModelPipeline::RunGraph(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) {
    if (!graph_)
        return Status(COSMO_NN_ERR_GRAPH_NOT_INIT, "Graph is not initialized");
    return graph_->Forward(inputs);
}

Status ModelPipeline::RunGraph(std::vector<std::vector<std::shared_ptr<Blob>>>& inputs) {
    if (!graph_)
        return Status(COSMO_NN_ERR_GRAPH_NOT_INIT, "Graph is not initialized");
    return graph_->Forward(inputs);
}

Status ModelPipeline::RunImageGraph(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) {
    if (!graph_)
        return Status(COSMO_NN_ERR_GRAPH_NOT_INIT, "Graph is not initialized");
    if (num_models_ == 1 || model_info_.reduce == "mean" || model_info_.reduce == "concat")
        return graph_->ForwardImageInputs(inputs);
    return graph_->Forward(inputs);
}

bool ModelPipeline::NeedsImageAuxiliary() const {
    // Pipelines without a graph keep the caller-supplied auxiliary inputs.
    return !graph_ || graph_->NeedsImageAuxiliary();
}

std::vector<std::shared_ptr<Blob>> ModelPipeline::GetGraphOutput() {
    if (!graph_)
        return {};
    return graph_->Output();
}

const Resize* ModelPipeline::GetRknnOutputResize() const {
    return graph_ ? graph_->GetRknnOutputResize() : nullptr;
}

void* ModelPipeline::GetSharedResource() {
    if (!graph_)
        return nullptr;
    return graph_->GetSharedResource();
}

// ─── Initialization Helpers ───────────────────────────────────────

void ModelPipeline::InitThresholdsAndLabels() {
    if (model_info_.config.instructions.empty()) {
        return;
    }

    const auto& instructions   = model_info_.config.instructions;
    const bool has_valid_infos = std::any_of(instructions.begin(), instructions.end(),
                                             [](const auto& inst) { return !inst.infos.empty(); });

    if (!has_valid_infos) {
        return;
    }

    std::vector<std::vector<float>> total_thresholds;
    ModelInfoUtils::GetSelectedThreshold(model_info_, total_thresholds);
    if (!total_thresholds.empty())
        selected_thresholds_ = total_thresholds.at(0);

    std::vector<std::vector<std::string>> total_classnames;
    ModelInfoUtils::GetSelectedClassName(model_info_, total_classnames);
    if (!total_classnames.empty())
        selected_classnames_ = total_classnames.at(0);

    ModelInfoUtils::GetSelectedIndex(model_info_, selected_indices_);
}

void ModelPipeline::InitNetInputSize() {
    if (model_info_.models.empty() || model_info_.models[0].input_node_infos.empty())
        return;

    auto image_node = model_info_.models[0].input_node_infos[0].name;
    ShapesMap inputs_map;
    auto status = ModelInfoUtils::GetInputShapesMap(model_info_.models[0], inputs_map);
    if (!bool(status))
        return;

    if (inputs_map.find(image_node) == inputs_map.end())
        return;
    const auto& shape = inputs_map.at(image_node);
    if (shape.size() >= 4)
        net_input_size_ = Size(shape[3], shape[2]);
}

// ─── Registry ───────────────────────────────────────────

ModelPipelineRegistry& ModelPipelineRegistry::Instance() {
    static ModelPipelineRegistry instance;
    return instance;
}

void ModelPipelineRegistry::Register(const std::string& type, PipelineCreatorFunc creator) {
    creators_[type] = creator;
}

ModelPipeline* ModelPipelineRegistry::Create(const std::string& type) {
    auto it = creators_.find(type);
    if (it == creators_.end())
        return nullptr;
    return it->second();
}

bool ModelPipelineRegistry::Has(const std::string& type) const {
    return creators_.find(type) != creators_.end();
}

std::vector<std::string> ModelPipelineRegistry::GetRegisteredTypes() const {
    std::vector<std::string> types;
    std::transform(creators_.begin(), creators_.end(), std::back_inserter(types),
                   [](const auto& pair) { return pair.first; });
    return types;
}

}  // namespace cosmo::nn
