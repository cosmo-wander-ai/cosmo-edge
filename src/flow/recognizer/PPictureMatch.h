#pragma once
#include "flow/action/PActionBase.h"
#include "infer/AiRecognizerInterface.h"

namespace cosmo {
class PPictureMatch : public PActionBase {
public:
    PPictureMatch(const std::string& taskId, ActionNode& action) : PActionBase(action, taskId) {}
    bool ActionInit() override;
    void ActionDestroy() override {
        comparator_.reset();
    }
    bool SetParam(const std::string&, std::vector<MsgDynamicKeyValue>& params) override;
    util::ErrorEnum HandPic(AlgDataPtr data) override;

private:
    bool body_{false};
    bool invert_{false};
    float threshold_{-1};
    std::vector<std::string> libraries_;
    AiRecognizerInterfacePtr comparator_;
};
}  // namespace cosmo
