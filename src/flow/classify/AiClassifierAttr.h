// AI classifier action for attribute-based classification.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "flow/classify/AiClassifierBase.h"

namespace cosmo {
class AiClassifierAttr : public AiClassifierBase {
public:
    AiClassifierAttr(const std::string& taskId, ActionNode& action);
    ~AiClassifierAttr() = default;

private:
    bool CheckDataAvailable(AlgDataPtr algData) override;
    void HandFramesEx(std::vector<AlgDataPtr> algDatas) override;
};
using AiClassifierAttrPtr = std::shared_ptr<AiClassifierAttr>;
}  // namespace cosmo
