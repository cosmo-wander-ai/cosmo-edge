// AI classifier action for per-target classification within the flow pipeline.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "flow/classify/AiClassifierBase.h"

namespace cosmo {
class AiClassifier : public AiClassifierBase {
public:
    AiClassifier(const std::string& taskId, ActionNode& action);
    ~AiClassifier() = default;

private:
    bool CheckDataAvailable(AlgDataPtr algData) override;
    void HandFramesEx(std::vector<AlgDataPtr> algDatas) override;
};
using AiClassifierPtr = std::shared_ptr<AiClassifier>;
}  // namespace cosmo
