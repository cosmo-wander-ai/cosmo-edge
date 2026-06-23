#pragma once

#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "nn/core/macros.h"
#include "nn/core/status.h"
#include "nn/utils/model_info_utils.h"
#include "nn/utils/net_utils.h"
#include "nn/utils/profiler.h"
#include "nn/utils/rect.h"

namespace cosmo::nn {

class Graph;

// ─── Pipeline Configuration Structs ────────────────────────────

struct PUBLIC PipelineModelConfig {
    std::string name;
    std::string file_name;
    std::string file_md5;
    int max_batch = 1;

    struct InputDef {
        std::string name;
        DimsVector shape;
        int data_type = 0;
    };
    struct OutputDef {
        std::string name;
        DimsVector shape;
        int data_type = 0;
    };

    std::vector<InputDef> inputs;
    std::vector<OutputDef> outputs;

    std::string params_json;
};

struct PUBLIC PipelineLabelInfo {
    std::string id;
    std::string name;
    std::vector<float> threshold;
};

struct PUBLIC PipelineConfig {
    std::string model_type;
    std::string chip_type;
    std::string algorithm_code;
    std::string version;
    std::string reduce;

    std::vector<PipelineModelConfig> models;
    std::vector<PipelineLabelInfo> labels;

    std::string extra_config_json;
};

// ─── Output Category ─────────────────────────────────────────────

enum class OutputCategory { DETECTION, CLASSIFICATION, FEATURE, KEYPOINTS, SEGMENTATION, TEXT, RAW };

// ─── Pipeline Base Class ───────────────────────────────────────
//
// Pipeline manages a model's complete lifecycle:
//   Init()        → parse config, build inference graph, load weights
//   Forward()     → preprocess + inference + postprocess
//   ParseOutput() → extract structured output from inference results
//
class PUBLIC ModelPipeline {
public:
    ModelPipeline();
    virtual ~ModelPipeline();

    virtual Status Init(const PipelineConfig& config, const std::string& model_path, DeviceType device_type,
                        int device_id, IProfiler* profiler = nullptr,
                        const std::string& tokenizer_path  = std::string(),
                        const std::string& word_table_path = std::string(), bool use_skip = false) = 0;

    virtual Status Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) = 0;

    virtual int GetMaxBatchSize() const              = 0;
    virtual std::string GetModelType() const         = 0;
    virtual OutputCategory GetOutputCategory() const = 0;

    // Typed output parsing — subclasses override as needed, default returns UNSUPPORTED
    virtual Status ParseDetectionOutput(std::vector<std::vector<ObjectInfoV1>>& outputs);
    virtual Status ParseClassifyOutput(std::vector<std::vector<ObjectInfoV1>>& outputs);
    virtual Status ParseFeatureOutput(std::vector<std::vector<float>>& outputs);
    virtual Status ParseKeypointsOutput(std::vector<std::vector<ObjectInfoV1>>& outputs);
    virtual Status ParseSegmentationOutput(std::vector<std::vector<uint8_t>>& outputs);
    virtual Status ParseTextOutput(std::vector<std::vector<std::string>>& outputs);
    virtual Status ParseOcrOutput(std::vector<std::vector<char>>& outputs);

    // Threshold and label management
    Status SetThreshold(int id, float threshold);
    Status SetThreshold(const std::string& label, float threshold);
    Status GetThreshold(int id, float* threshold);
    Status GetThreshold(const std::string& label, float* threshold);
    Status GetCategoryInfo(int class_id, CategoryInfo& info);
    Status GetCategoryInfo(const std::string& label, CategoryInfo& info);

    std::vector<float> GetScoreLevels() const;
    std::vector<std::string> GetSelectedClassnames() const;

protected:
    // Subclasses call this after building CombinedModelInfo to initialize the inference graph
    Status InitGraph(const std::string& model_path, DeviceType device_type, int device_id,
                     IProfiler* profiler, const std::string& tokenizer_path, bool use_skip);

    // Run the inference graph
    Status RunGraph(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs);
    Status RunGraph(std::vector<std::vector<std::shared_ptr<Blob>>>& inputs);

    // Get inference graph output
    std::vector<std::shared_ptr<Blob>> GetGraphOutput();

    // Get Graph's SharedResource (used by DINO and models that need a tokenizer)
    void* GetSharedResource();

    // Subclasses configure the inference graph through this variable
    CombinedModelInfo model_info_;

    // Runtime state
    std::vector<Size> image_sizes_;
    std::vector<Rect2i> rects_;
    Size net_input_size_;
    int num_models_ = 0;

    // Labels and thresholds
    std::vector<float> selected_thresholds_;
    std::vector<int> selected_indices_;
    std::vector<std::string> selected_classnames_;

    // OCR word table
    std::vector<std::string> ocr_words_;

    // Called in subclass Init before calling InitGraph
    void InitThresholdsAndLabels();
    void InitNetInputSize();

private:
    std::unique_ptr<Graph> graph_;
};

// ─── Registry ─────────────────────────────────────────────────

using PipelineCreatorFunc = std::function<ModelPipeline*()>;

class PUBLIC ModelPipelineRegistry {
public:
    static ModelPipelineRegistry& Instance();

    void Register(const std::string& type, PipelineCreatorFunc creator);
    ModelPipeline* Create(const std::string& type);
    bool Has(const std::string& type) const;
    std::vector<std::string> GetRegisteredTypes() const;

private:
    ModelPipelineRegistry() = default;
    std::map<std::string, PipelineCreatorFunc> creators_;
};

template <typename T>
class PipelineRegistrar {
public:
    explicit PipelineRegistrar(const std::string& type) {
        ModelPipelineRegistry::Instance().Register(type, []() -> ModelPipeline* { return new T(); });
    }
};

#define COSMO_NN_PIPELINE_REGISTRAR_NAME_(a, b) g_pipeline_registrar_##a##_##b
#define COSMO_NN_PIPELINE_REGISTRAR_NAME(a, b) COSMO_NN_PIPELINE_REGISTRAR_NAME_(a, b)
#define REGISTER_MODEL_PIPELINE(type_name, PipelineClass)                                                    \
    static ::cosmo::nn::PipelineRegistrar<PipelineClass> COSMO_NN_PIPELINE_REGISTRAR_NAME(                   \
        PipelineClass, __LINE__)(type_name)

}  // namespace cosmo::nn
