// PQwen3VLWorker — Qwen3VL vision-language model wrapper for single-image analysis mode.

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "flow/action/PActionBase.h"
#include "flow/common/VisualJudgment.h"
#include "flow/qwen3vl/Qwen3VLWorker.h"

namespace cosmo {

class PQwen3VLWorker : public PActionBase {
public:
    explicit PQwen3VLWorker(ActionNode& action, const std::string& task_id = "",
                            bool has_upstream_targets = false);
    ~PQwen3VLWorker() override;

    bool ActionInit() override;
    void ActionDestroy() override;
    util::ErrorEnum HandPic(AlgDataPtr alg_data) override;

    bool ModifyParam(const std::string& task_id, std::vector<MsgDynamicKeyValue>& params) override;
    bool SetParam(const std::string& task_id, std::vector<MsgDynamicKeyValue>& params) override;

    bool SetArea(const std::string& task_id, std::vector<MsgTaskArea>& areas,
                 std::vector<MsgTaskArea>& shielded_areas) override;

private:
    void RebuildVisualJudgment();
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
    VisualParameters visual_parameters_;
    std::vector<MsgTaskArea> areas_;
    std::shared_ptr<VisualJudgment> visual_judgment_;
    bool stopped_{false};
    bool has_upstream_targets_{false};
};

using PQwen3VLWorkerPtr = std::shared_ptr<PQwen3VLWorker>;

}  // namespace cosmo
