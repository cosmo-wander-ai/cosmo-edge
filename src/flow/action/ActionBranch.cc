// ActionBranch — Action Branch implementation.

#include "flow/action/ActionBranch.h"

#include <algorithm>

#include "flow/common/AlgDataRecord.h"
#include "util/Keys.h"
#include "util/Log.h"
#include "util/SafeParse.h"

static constexpr const char* kTag = "ActionBranch ";
namespace cosmo {

ActionBranch::~ActionBranch() {
    LOG_INFO("{}Task:{} Stop", kTag, task_id);
    Stop();
    LOG_INFO("{}Task:{} Delete", kTag, task_id);
}

ActionBranch::ActionBranch(const std::string& taskId, ActionNode& action)
    : AlgActionBase(AlgActionType::AlgActionBAActionBranch, action, "", taskId),
      logic_(action.configObject.condition),
      overview_rec_inst_(taskId, "branch") {
    action_status = util::ErrorEnum::ActionReady;
    LOG_INFO("{}Task:{} Init, logic.type:{} KeyL:{} KeyR:{}", kTag, task_id, logic_.type, logic_.keyL,
             logic_.keyR);
}

/*
custom.detectLabels
*/
bool ActionBranch::AnalysisCustomKey(MsgDynamicKeyValue& param, MsgDynamicKeyValue& localParamEl) {
    if (param.keys.empty()) {
        LOG_WARN(
            "ModifyParam "
            "[{}] param.keys is Empty",
            task_id);
        return false;
    }
    if (param.keys.size() != 2) {
        LOG_DEBUG(
            "ModifyParam "
            "[{}] Set {} Failed. key size:{}",
            task_id, param.key, param.keys.size());
        return false;
    }

    if (key::CUSTOM != param.keys[0]) {
        LOG_DEBUG(
            "ModifyParam "
            "[{}] param.keys[0] is Not {}",
            task_id, key::CUSTOM);
        return false;
    }

    localParamEl.key   = param.key;
    localParamEl.value = param.value;
    auto values        = util::Split(param.value.ToRefString(), ",");
    localParamEl.values.assign(values.begin(), values.end());

    LOG_INFO(
        "ModifyParam "
        "[{}] Set key:{} value:{} size:{}",
        task_id, localParamEl.key, localParamEl.value, localParamEl.values.size());

    return true;
}

// Modify parameter - modify based on existing parameters
bool ActionBranch::ModifyParam(const std::string& /*channelId*/, const std::string& /*taskId*/,
                               std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    for (auto& param : params) {
        MsgDynamicKeyValue localNewKeyValueEl;
        if (AnalysisCustomKey(param, localNewKeyValueEl)) {
            auto it = std::find_if(params_.customs.begin(), params_.customs.end(),
                                   [&localNewKeyValueEl](const auto& localParamEl) {
                                       return localParamEl.key == localNewKeyValueEl.key;
                                   });
            if (it != params_.customs.end()) {
                it->value  = localNewKeyValueEl.value;
                it->values = localNewKeyValueEl.values;
                it->keys   = localNewKeyValueEl.keys;
            } else {
                params_.customs.push_back(localNewKeyValueEl);
            }
        }
    }

    return false;
}

// Set parameter - clear previous parameters and set new ones completely
bool ActionBranch::SetParam(const std::string& /*channelId*/, const std::string& /*taskId*/,
                            std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    // Clear previous parameters first
    params_.customs.clear();
    for (auto& param : params) {
        MsgDynamicKeyValue localParamKeyValueEl;
        if (AnalysisCustomKey(param, localParamKeyValueEl)) {
            // Add parameters
            params_.customs.push_back(localParamKeyValueEl);
        }
    }

    return false;
}

void ActionBranch::HandFrame(AlgDataPtr algData) {
    if (!algData) {
        invalid_frame_cnt += 1;
        if (0 == invalid_frame_cnt % 100) {
            LOG_WARN("{}[{}] Filter {} Frames", kTag, task_id, invalid_frame_cnt);
        }
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }

    // No logic, just branch
    if (LogicType::INVALID == logic_.type) {
        distribute_frames_ += 1;
        distributor->DistributorData(AlgDataCopy(algData));
    } else {
        if (GetLogicResult(logic_)) {
            distribute_frames_ += 1;
            distributor->DistributorData(AlgDataCopy(algData));
        }
    }

    handle_frame_cnt += 1;
    if (0 == handle_frame_cnt % 100) {
        LOG_INFO("{}[{}] Handle {} Frames, Distribute {} Frames", kTag, task_id, handle_frame_cnt,
                 distribute_frames_);
    }
    action_status = util::ErrorEnum::Success;
}

MsgOverviewMem ActionBranch::GetOverviewInfo(const std::string& /*channelId*/, const std::string& /*taskId*/,
                                             int64_t streamIndex, int64_t from, int64_t to) {
    return overview_rec_inst_.GetOverviewInfo(streamIndex, from, to);
}

bool ActionBranch::GetLogicResult(LogicCalc& logic, bool debug) {
    bool ret = false;

    // Include or Not Include
    if ((LogicType::Include == logic.type) || (LogicType::NonInclude == logic.type)) {
        ret = CalcIncludeOperation(logic);
    }
    // Arithmetic operation
    else if (logic.list.empty()) {
        ret = CalcArithmeticOperation(logic);
    }
    // Logic judgment
    else {
        ret = CalcLogicOperation(logic);
    }
    if (debug) {
        LOG_INFO("{} listSize:{} Get Ret:{}", LogicString(logic.type), logic.list.size(), ret);
    }
    return ret;
}

bool ActionBranch::ValueIsInclude(const std::string& key, const std::string& value) {
    auto it = std::find_if(params_.customs.begin(), params_.customs.end(),
                           [&key](const auto& localParam) { return localParam.key == key; });
    if (it != params_.customs.end()) {
        return std::any_of(it->values.begin(), it->values.end(),
                           [&value](const std::string& element) { return element == value; });
    }
    return false;
}

// Calculate whether local parameters are included or not
bool ActionBranch::CalcIncludeOperation(LogicCalc& logic) {
    switch (logic.type) {
        case LogicType::Include:
            return ValueIsInclude(logic.keyL, logic.keyR);
        case LogicType::NonInclude:
            return !ValueIsInclude(logic.keyL, logic.keyR);
        default: {
            LOG_WARN("{}Task:{} LogicType {} Error", kTag, task_id, logic.type);
        } break;
    }

    return false;
}

// Arithmetic operation
bool ActionBranch::CalcArithmeticOperation(LogicCalc& logic) {
    auto lValue   = GetLogicValue(logic.keyL, logic.keyLElements);
    auto rValue   = GetLogicValue(logic.keyR, logic.keyRElements);
    float dlValue = util::ParseFloat(lValue);
    float drValue = util::ParseFloat(rValue);
    // logic.keyRElements.size(),logic.keyRElements.empty(),lValue, rValue, dlValue, drValue);
    switch (logic.type) {
        case LogicType::Equal:
            return lValue == rValue;
        case LogicType::NEQ:
            return lValue != rValue;
        case LogicType::Greater:
            return dlValue > drValue;
        case LogicType::GE:
            return dlValue >= drValue;
        case LogicType::Less:
            return dlValue < drValue;
        case LogicType::LE:
            return dlValue <= drValue;
        default: {
            LOG_WARN("{}Task:{} LogicType {} Error", kTag, task_id, logic.type);
        } break;
    }

    return false;
}

// Logic operation
bool ActionBranch::CalcLogicOperation(LogicCalc& logic) {
    bool resInit = false;
    if (LogicType::OR == logic.type) {
        resInit = false;
        if (logic.list.size() < 1) {
            LOG_WARN("{}Task:{} LOGIC OR But The Queue Size is {}", kTag, task_id, logic.list.size());
            return false;
        }
    } else if (LogicType::AND == logic.type) {
        resInit = true;
        if (logic.list.size() < 1) {
            LOG_WARN("{}Task:{} LOGIC AND But The Queue Size is {}", kTag, task_id, logic.list.size());
            return false;
        }
    } else if (LogicType::NOR == logic.type) {
        resInit = false;
        if (logic.list.size() != 1) {
            LOG_WARN("{}Task:{} LOGIC NOR But The Queue Size is {}", kTag, task_id, logic.list.size());
            return false;
        }
    } else {
        LOG_WARN("{}Task:{} LOGIC Type Error:{}", kTag, task_id, logic.type);
        return false;
    }

    for (auto& logicItem : logic.list) {
        switch (logic.type) {
            case LogicType::OR: {
                resInit = resInit || GetLogicResult(logicItem);
            } break;
            case LogicType::AND: {
                resInit = resInit && GetLogicResult(logicItem);
            } break;
            case LogicType::NOR: {
                // Handled in NOT logic, skip here
                return GetLogicResult(logicItem);
            } break;
            default: {
                LOG_WARN("{}Task:{} LogicType Error:{}", kTag, task_id, logic.type);
            }
                // Must be logic operation if with list, otherwise return false
                return false;
        }
    }

    return resInit;
}

std::string ActionBranch::GetParamValue(const std::string& key) {
    auto it = std::find_if(params_.customs.begin(), params_.customs.end(),
                           [&key](const auto& localParam) { return localParam.key == key; });
    if (it != params_.customs.end()) {
        return it->value;
    }
    return "";
}

std::string ActionBranch::GetLogicValue(const std::string& key, const std::vector<std::string>& keys) {
    // Directly a value
    if (keys.size() < 2) {
        return key;
    }
    // custom.xxx case
    return GetParamValue(key);
}
}  // namespace cosmo