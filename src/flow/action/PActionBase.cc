// PActionBase — Image algorithm

#include "flow/action/PActionBase.h"

#include "util/Log.h"
#include "util/dto/ActionCodes.h"

namespace cosmo {

PActionBase::PActionBase(ActionNode& action, const std::string& taskId) : task_id_(taskId), action_(action) {}

bool PActionBase::ActionInit() {
    LOG_WARN("[{}]-{}/{}-Flow:{} NEED Override Function", GetTaskId(), GetActionId(), GetName(),
             GetFlowActionId());
    return false;
}

void PActionBase::ActionDestroy() {
    LOG_WARN("[{}]-{}/{}-Flow:{} NEED Override Function", GetTaskId(), GetActionId(), GetName(),
             GetFlowActionId());
    return;
}

util::ErrorEnum PActionBase::HandPic(AlgDataPtr /*algData*/) {
    LOG_WARN("[{}]-{}/{}-Flow:{} NEED Override Function", GetTaskId(), GetActionId(), GetName(),
             GetFlowActionId());
    return util::ErrorEnum::NotImplement;
}

bool PActionBase::ModifyParam(const std::string& /*taskId*/, std::vector<MsgDynamicKeyValue>& params) {
    LOG_WARN("[Task:{} Action:{}] Have Not ModifyParam Override Function. params size:{}", task_id_,
             action_.actionId, params.size());
    return false;
}

bool PActionBase::SetParam(const std::string& /*taskId*/, std::vector<MsgDynamicKeyValue>& params) {
    LOG_WARN("[Task:{} Action:{}] Have Not SetParam Override Function. params size:{}", task_id_,
             action_.actionId, params.size());
    return false;
}

bool PActionBase::SetArea(const std::string& /*taskId*/, std::vector<MsgTaskArea>& /*areas*/,
                          std::vector<MsgTaskArea>& /*shieldedAreas*/) {
    LOG_WARN("[Task:{} Action:{}] Have Not SetArea Override Function. ", task_id_, action_.actionId);
    return false;
}
}  // namespace cosmo
