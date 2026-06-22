// Image algorithm logical judgment

#include "flow/logical/PLogicalJudgment.h"

#include <algorithm>

#include "util/Keys.h"
#include "util/Log.h"
#include "util/SafeParse.h"

namespace cosmo {

PLogicalJudgment::~PLogicalJudgment() {
    LOG_INFO("[{} {}] Stop", GetTaskId(), GetFlowActionId());
    LOG_INFO("[{} {}] Delete", GetTaskId(), GetFlowActionId());
}

PLogicalJudgment::PLogicalJudgment(const std::string& taskId, ActionNode& action)
    : PActionBase(action, taskId),
      logic_(action.configObject.condition),
      calc_engine_(
          taskId + " " + action.flowActionId,
          [this](const std::string& label, float& value) -> bool {
              auto it = std::find_if(params_.params.begin(), params_.params.end(),
                                     [&label](const auto& p) { return p.label == label; });
              if (it != params_.params.end()) {
                  value = it->confidence;
                  return true;
              }
              return false;
          },
          [this](const std::string& key, const std::string& value) -> bool {
              auto it = std::find_if(params_.customs.begin(), params_.customs.end(),
                                     [&key](const auto& c) { return c.key == key; });
              if (it != params_.customs.end()) {
                  return std::find(it->values.begin(), it->values.end(), value) != it->values.end();
              }
              return false;
          }) {
    action_status_ = util::ErrorEnum::ActionReady;
    LOG_INFO("[{} {}] Init Logic:{} KeyL:{} KeyR:{}", GetTaskId(), GetFlowActionId(),
             LogicString(logic_.type), logic_.keyL, logic_.keyR);
}

bool PLogicalJudgment::ActionInit() {
    action_status_ = util::ErrorEnum::AI_INST_CREATED;
    LOG_INFO("[{} {}] Init Sdk", GetTaskId(), GetFlowActionId());
    return true;
}

bool PLogicalJudgment::ValidKey(MsgDynamicKeyValue& param) const {
    if (param.keys.empty()) {
        LOG_WARN(
            "ModifyParam "
            "[{} {}] param.keys is Empty",
            GetTaskId(), GetFlowActionId());
        return false;
    }
    if (param.keys.size() != 3) {
        LOG_DEBUG(
            "ModifyParam "
            "[{} {}] Set {} Failed. key size:{}",
            GetTaskId(), GetFlowActionId(), param.key, param.keys.size());
        return false;
    }

    if ((key::AI_PARAM != param.keys[0]) || (key::CONFIDENCE != param.keys[2])) {
        LOG_DEBUG(
            "ModifyParam "
            "[{} {}] param.keys[0]:{} is Not {}",
            GetTaskId(), GetFlowActionId(), param.keys[0], key::AI_PARAM);
        return false;
    }

    return true;
}

bool PLogicalJudgment::AnalysisKey(MsgDynamicKeyValue& param) {
    if (!ValidKey(param)) {
        return false;
    }

    AiConfidence localParamEl;
    localParamEl.label      = param.keys[1];
    localParamEl.confidence = util::ParseFloat(param.value.ToString());
    LOG_INFO(
        "ModifyParam "
        "[{} {}] Set Param {} To {}",
        GetTaskId(), GetFlowActionId(), param.key, param.value);
    auto it = std::find_if(params_.params.begin(), params_.params.end(),
                           [&localParamEl](const auto& p) { return p.label == localParamEl.label; });
    if (it != params_.params.end()) {
        it->confidence = localParamEl.confidence;
        return true;
    }
    params_.params.push_back(localParamEl);
    return true;
}

bool PLogicalJudgment::AnalysisCustomKey(MsgDynamicKeyValue& param) {
    if (param.keys.empty()) {
        LOG_WARN(
            "ModifyParam "
            "[{} {}] param.keys is Empty",
            GetTaskId(), GetFlowActionId());
        return false;
    }
    if (param.keys.size() != 2) {
        LOG_DEBUG(
            "ModifyParam "
            "[{} {}] Set {} Failed. key size:{}",
            GetTaskId(), GetFlowActionId(), param.key, param.keys.size());
        return false;
    }

    if (key::CUSTOM != param.keys[0]) {
        LOG_DEBUG(
            "ModifyParam "
            "[{} {}] param.keys[0] is Not {}",
            GetTaskId(), GetFlowActionId(), key::CUSTOM);
        return false;
    }

    MsgDynamicKeyValue localParamEl;
    localParamEl.key   = param.key;
    localParamEl.value = param.value;
    auto values        = util::Split(param.value.ToRefString(), ",");
    localParamEl.values.assign(values.begin(), values.end());
    LOG_INFO(
        "ModifyParam "
        "[{} {}] Set Param {} To {}",
        GetTaskId(), GetFlowActionId(), param.key, param.value);

    auto it = std::find_if(
        params_.customs.begin(), params_.customs.end(),
        [&localParamEl](const auto& customParam) { return customParam.key == localParamEl.key; });
    if (it != params_.customs.end()) {
        it->value  = localParamEl.value;
        it->values = localParamEl.values;
        return true;
    }
    params_.customs.push_back(localParamEl);

    return true;
}

// Modify parameters based on existing ones
bool PLogicalJudgment::ModifyParam(const std::string& /*taskId*/, std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    for (auto& param : params) {
        AnalysisKey(param);
        AnalysisCustomKey(param);
    }
    return true;
}

// Set parameters - clear previous ones and set fully new ones
bool PLogicalJudgment::SetParam(const std::string& /*taskId*/, std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    params_ = {};
    for (auto& param : params) {
        AnalysisKey(param);
        AnalysisCustomKey(param);
    }
    return true;
}

util::ErrorEnum PLogicalJudgment::HandPic(AlgDataPtr algData) {
    if (!algData) {
        return util::ErrorEnum::FlowDataInvalid;
    }
    algData->bHaveLogic = true;
    if (!algData->chanDataDetect.detRet) {
        return util::ErrorEnum::FlowDataNull;
    }

    for (auto& target : algData->chanDataDetect.detRet->targets) {
        if (!target.bFilter)
            target.bLogicResult = calc_engine_.GetLogicResult(target, logic_);
    }

    return util::ErrorEnum::Success;
}

}  // namespace cosmo