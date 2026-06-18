#pragma once

#include <memory>
#include <mutex>
#include <string>

#include "nn/core/status.h"
#include "nn/utils/net_utils.h"
#include "nn/utils/profiler.h"
#include "nn/utils/rect.h"

namespace cosmo::nn {

class ModelPipeline;

class PUBLIC DefaultComponent {
public:
    DefaultComponent(std::string json_path, std::string model_path, DeviceType device_type,
                     IProfiler* profiler = nullptr, std::string tokenizer_path = std::string(),
                     std::string word_table_path = std::string(), int device_id = 0,
                     bool use_skip = false) noexcept(false);

    ~DefaultComponent();

    DefaultComponent(DefaultComponent const& other)             = delete;
    DefaultComponent& operator=(DefaultComponent const& other)  = delete;
    DefaultComponent(DefaultComponent const&& other)            = delete;
    DefaultComponent& operator=(DefaultComponent const&& other) = delete;

    Status Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs);

    template <typename T>
    Status ParseOutput(std::vector<std::vector<T>>& outputs);

    int GetMaxBatchSize() const;

    Status SetThreshold(int id, float threshold);
    Status SetThreshold(std::string label, float threshold);
    Status GetThreshold(int id, float* threshold);
    Status GetThreshold(std::string label, float* threshold);

    Status GetCategoryInfo(int class_id, CategoryInfo& info);
    Status GetCategoryInfo(std::string label, CategoryInfo& info);

    std::vector<float> GetScoreLevels() const;
    std::vector<std::string> GetSelectedClassnames() const;

private:
    static std::mutex mutex;
    std::unique_ptr<ModelPipeline> pipeline_;
};

}  // namespace cosmo::nn
