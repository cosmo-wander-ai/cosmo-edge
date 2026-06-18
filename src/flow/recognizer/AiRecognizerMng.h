// AiRecognizerMng — Instance manager for AiRecognizer actions.

#pragma once

#include <map>
#include <memory>
#include <string>

#include "flow/action/ActionInstMngBase.h"
#include "flow/recognizer/AiRecognizer.h"

namespace cosmo {
class AiRecognizerMng : public IMngStatusProvider {
public:
    AiRecognizerMng();
    ~AiRecognizerMng();

    AiRecognizerPtr GetInst(const std::string& task_id, const std::string& alg_code,
                            ActionNode& action_param);
    bool DeleteInst(AiRecognizerPtr inst, const std::string& task_id, const std::string& alg_code);

    void QueueStatus(std::vector<AlgActionDataQueueStatus>& que_status,
                     unsigned int duration_sec = 30) override;
    void ActionInfo(std::vector<ActionRuntimeInfo>& action_info) override;

private:
    std::shared_mutex mtx_;
    std::map<std::string, AiRecognizerPtr> insts_;  // Key: Task ID, Value: Algorithm instance
};
}  // namespace cosmo
