// PQwen3VLWorker — Qwen3VL vision-language model wrapper for single-image analysis mode.

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "flow/action/PActionBase.h"
#include "flow/qwen3vl/Qwen3VLWorker.h"

namespace cosmo {

class PQwen3VLWorker : public PActionBase {
public:
    explicit PQwen3VLWorker(ActionNode& action, const std::string& task_id = "");
    ~PQwen3VLWorker() override;

    bool ActionInit() override;
    void ActionDestroy() override;
    util::ErrorEnum HandPic(AlgDataPtr alg_data) override;

    bool ModifyParam(const std::string& task_id, std::vector<MsgDynamicKeyValue>& params) override;
    bool SetParam(const std::string& task_id, std::vector<MsgDynamicKeyValue>& params) override;

private:
    bool ValidKey(MsgDynamicKeyValue& param);
    bool AnalysisKey(MsgDynamicKeyValue& param);
    void ApplyGenerationStyle();

    std::shared_mutex mtx_;

    // Parameter configuration
    std::string prompt_{""};
    bool advanced_mode_{false};
    bool worker_registered_{false};  // True after NotifyWorkerStart(); guards NotifyWorkerStop()
    Qwen3VLGenerationStyle generation_style_{Qwen3VLGenerationStyle::STANDARD};
    Qwen3VLGenerationParam gen_param_;
    OpenAiVlmConfig open_ai_config_;
};

using PQwen3VLWorkerPtr = std::shared_ptr<PQwen3VLWorker>;

}  // namespace cosmo
