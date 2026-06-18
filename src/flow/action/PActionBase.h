// Image algorithm

#pragma once

#include <string>
#include <vector>

#include "flow/common/AlgDataUnit.h"
#include "util/MsgBaseTypes.h"
#include "util/MsgDynamicElement.h"
#include "util/dto/AlgDataQueueTypes.h"
#include "util/dto/AlgorithmMsgTypes.h"
#include "util/dto/FilterTypes.h"
#include "util/dto/OverviewTypes.h"
#include "util/dto/TaskAreaTypes.h"

namespace cosmo {

class PActionBase {
public:
    explicit PActionBase(ActionNode& pAction, const std::string& taskId = "");
    virtual ~PActionBase() = default;

    // Resource initialization
    virtual bool ActionInit();
    // Resource destruction
    virtual void ActionDestroy();
    // Process data
    virtual util::ErrorEnum HandPic(AlgDataPtr algData);

    // Modify parameter - modify based on existing parameters
    virtual bool ModifyParam(const std::string& taskId, std::vector<MsgDynamicKeyValue>& params);
    // Set parameter - clear previous parameters and set new ones completely
    virtual bool SetParam(const std::string& taskId, std::vector<MsgDynamicKeyValue>& params);

    virtual bool SetArea(const std::string& taskId, std::vector<MsgTaskArea>& areas,
                         std::vector<MsgTaskArea>& shieldedAreas);

    const std::string GetAtomicCode() const {
        return action_.atomicCode.empty() ? action_.atomAlgName : action_.atomicCode;
    };
    const std::string& GetTaskId() const {
        return task_id_;
    };
    const std::string& GetActionId() const {
        return action_.actionId;
    };
    const std::string& GetName() const {
        return action_.actionName;
    };
    const std::string& GetFlowActionId() const {
        return action_.flowActionId;
    };

private:
    std::string task_id_;
    ActionNode action_;
};

using PActionBasePtr = std::shared_ptr<PActionBase>;

}  // namespace cosmo
