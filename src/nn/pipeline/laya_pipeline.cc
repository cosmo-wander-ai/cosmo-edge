#include "nn/pipeline/laya_pipeline.h"

#ifdef COSMO_NN_USE_SOPHON_BACKEND
#include "nn/device/sophon/laya/laya_runner.h"
#endif

namespace cosmo::nn {
LayaPipeline::LayaPipeline()  = default;
LayaPipeline::~LayaPipeline() = default;
Status LayaPipeline::Init(const PipelineConfig& config, const std::string& model_path, DeviceType device_type,
                          int device_id, IProfiler*, const std::string& tokenizer_path, const std::string&,
                          bool) {
#ifdef COSMO_NN_USE_SOPHON_BACKEND
    if (device_type != DEVICE_SOPHON_TPU || config.chip_type != "BM1688" || config.models.size() != 3)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "Laya requires BM1688 and its three-segment model package");
    auto runner = std::make_unique<LayaRunner>();
    auto status = runner->Init(model_path, tokenizer_path, device_id);
    if (bool(status))
        runner_ = std::move(runner);
    return status;
#else
    return Status(COSMO_NN_ERR_NET, "Laya requires the Sophon BM1688 backend");
#endif
}
Status LayaPipeline::Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) {
#ifdef COSMO_NN_USE_SOPHON_BACKEND
    if (runner_)
        return runner_->Run(std::vector<std::vector<std::shared_ptr<Blob>>>(inputs));
#endif
    return Status(COSMO_NN_ERR_GRAPH_NOT_INIT, "Laya is not initialized");
}
Status LayaPipeline::ParseTextOutput(std::vector<std::vector<std::string>>& outputs) {
#ifdef COSMO_NN_USE_SOPHON_BACKEND
    if (runner_) {
        outputs = runner_->Outputs();
        return COSMO_NN_OK;
    }
#endif
    return Status(COSMO_NN_ERR_GRAPH_NOT_INIT, "Laya is not initialized");
}
Status LayaPipeline::PrepareText(const std::string& input, std::string& output) {
#ifdef COSMO_NN_USE_SOPHON_BACKEND
    if (runner_)
        return runner_->PrepareText(input, output);
#endif
    return Status(COSMO_NN_ERR_GRAPH_NOT_INIT, "Laya is not initialized");
}
REGISTER_MODEL_PIPELINE("laya_v", LayaPipeline);
}  // namespace cosmo::nn
