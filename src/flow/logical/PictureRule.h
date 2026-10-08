#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>

#include "flow/action/PActionBase.h"

namespace cosmo {

// A missing operand is unknown, not false. NOT must preserve that distinction.
std::optional<bool> EvaluatePictureRule(const LogicCalc& rule, const AlgData& data,
                                        const AiDetectRstEl* target,
                                        const std::map<std::string, std::string>& params);

class PictureRuleAction : public PActionBase {
public:
    PictureRuleAction(const std::string& taskId, ActionNode& action);
    bool ActionInit() override {
        return true;
    }
    void ActionDestroy() override {}
    bool SetParam(const std::string&, std::vector<MsgDynamicKeyValue>& params) override;
    util::ErrorEnum HandPic(AlgDataPtr data) override;

private:
    LogicCalc condition_;
    std::set<std::string> filter_labels_;
    std::map<std::string, std::string> params_;
};

}  // namespace cosmo
