#pragma once
#include "flow/action/PActionBase.h"
#include "infer/AiOcrWordClassifierUnify.h"

namespace cosmo {
class POcr : public PActionBase {
public:
    POcr(const std::string& taskId, ActionNode& action) : PActionBase(action, taskId) {}
    bool ActionInit() override;
    void ActionDestroy() override {
        instance_.reset();
    }
    util::ErrorEnum HandPic(AlgDataPtr data) override;

private:
    AiOcrWordClassifierUnifyPtr instance_;
};
}  // namespace cosmo
