#pragma once

#include <optional>

#include "flow/action/PActionBase.h"
#include "infer/AiRecognizerInterface.h"

namespace cosmo {
// Request-local peer features are passed explicitly; no reference image is retained by this action.
class PPicturePairMatch : public PActionBase {
public:
    PPicturePairMatch(const std::string& taskId, ActionNode& action) : PActionBase(action, taskId) {}
    bool ActionInit() override;
    void ActionDestroy() override {
        comparator_.reset();
    }
    bool SetParam(const std::string&, std::vector<MsgDynamicKeyValue>& params) override;
    util::ErrorEnum HandPic(AlgDataPtr) override {
        return util::ErrorEnum::PicturePairInputRequired;
    }
    virtual util::ErrorEnum Compare(const AiFeature& left, const AiFeature& right, double& score);
    const std::optional<double>& Threshold() const {
        return threshold_;
    }

private:
    bool face_{true};
    std::optional<double> threshold_;
    AiRecognizerInterfacePtr comparator_;
};
}  // namespace cosmo
